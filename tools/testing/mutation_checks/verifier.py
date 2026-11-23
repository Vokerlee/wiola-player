"""Compose source mutation, build and test protocols without filesystem guesses."""

import json
from pathlib import Path
from typing import Mapping, Protocol

from tools.architecture.ownership.registry import OwnershipRegistry
from tools.testing.gtest.runner import GoogleTestRunner
from tools.testing.source_mutation.mutation import SourceConflict, SourceMutation

from .catalog import MutationCase


class BuildPort(Protocol):
    def build(self, name: str, log: Path) -> None: ...
    def executable(self, name: str) -> Path: ...


class MutationVerifier:
    def __init__(
        self,
        root: Path,
        registry: OwnershipRegistry,
        build: BuildPort,
        runner: GoogleTestRunner,
        artifacts: Path,
        revision: str,
        environment: Mapping[str, str],
        *,
        timeout: float = 60,
    ):
        self.root = root.resolve()
        self.registry, self.build, self.runner = registry, build, runner
        self.artifacts, self.revision = artifacts.resolve(), revision
        self.environment, self.timeout = dict(environment), timeout

    def verify(self, case: MutationCase) -> dict:
        directory = self.artifacts / case.identity
        directory.mkdir(parents=True, exist_ok=True)
        evidence = {
            "seed": case.identity,
            "source_owner": case.source_owner,
            "target": case.target,
            "filter": case.selection,
            "revision": self.revision,
            "killed_by_assertion": False,
            "restored": False,
        }

        def execute(phase):
            self.build.build(case.target, directory / (phase + "-build.log"))
            return self.runner.run(
                self.build.executable(case.target),
                case.selection,
                directory / phase,
                environment=self.environment,
                revision=self.revision,
                timeout=self.timeout,
            )

        try:
            source = case.source(self.registry, self.root)
            mutation = SourceMutation(
                source, case.anchor.encode("utf-8"), case.replacement.encode("utf-8")
            )
            evidence.update(
                source=source.relative_to(self.root).as_posix(),
                original_sha256=mutation.original_sha256,
                mutant_sha256=mutation.mutant_sha256,
            )
            if not execute("original").passed:
                raise RuntimeError("Original contract did not pass: " + case.identity)
            try:
                with mutation:
                    evidence["killed_by_assertion"] = execute(
                        "mutant"
                    ).assertion_failure
                    if not evidence["killed_by_assertion"]:
                        raise RuntimeError(
                            "Mutation survived, crashed or lacked assertion evidence: "
                            + case.identity
                        )
            finally:
                # Even a failed mutant build/process must leave a rebuilt, checked original.
                # A concurrent writer's conflicting source is never compiled as our restoration.
                if not mutation.restored:
                    raise SourceConflict(
                        "Cannot qualify a source that was not restored: " + str(source)
                    )
                if not execute("restored").passed:
                    raise RuntimeError(
                        "Restored contract did not pass: " + case.identity
                    )
                evidence["restored"] = True
                evidence["restored_sha256"] = mutation.original_sha256
        except BaseException as error:
            evidence["error"] = {"type": type(error).__name__, "message": str(error)}
            raise
        finally:
            (directory / "mutation.json").write_text(
                json.dumps(evidence, indent=2) + "\n", encoding="utf-8"
            )
        return evidence
