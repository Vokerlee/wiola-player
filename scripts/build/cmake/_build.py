"""Use configured target metadata instead of executable path conventions."""

import os
from pathlib import Path
import subprocess

from ._inventory import CMakeInventory


class CMakeBuild:
    def __init__(
        self,
        inventory: CMakeInventory,
        *,
        jobs: int | None = None,
        timeout: float = 900,
        command: tuple[str, ...] = ("cmake",),
    ):
        if jobs is None:
            configured = os.environ.get("CMAKE_BUILD_PARALLEL_LEVEL")
            jobs = (
                int(configured)
                if configured
                else (
                    len(os.sched_getaffinity(0))
                    if hasattr(os, "sched_getaffinity")
                    else os.cpu_count() or 1
                )
            )
        if jobs < 1 or timeout <= 0:
            raise ValueError("CMake build parallelism and timeout must be positive")
        if inventory.build_directory is None:
            raise ValueError(
                "Build execution needs an inventory with a configured directory"
            )
        self.directory = inventory.build_directory.resolve()
        self.inventory = inventory
        self.jobs = jobs
        self.timeout = timeout
        self.command = command
        self._targets = {target.name: target for target in inventory.targets}
        if len(self._targets) != len(inventory.targets):
            raise ValueError("Configured CMake targets must have unique identities")

    def executable(self, name: str) -> Path:
        target = self._target(name)
        artifacts = tuple(
            path
            for path in target.artifacts
            if target.name_on_disk is None or path.name == target.name_on_disk
        )
        if target.kind != "EXECUTABLE" or len(artifacts) != 1:
            raise ValueError(
                "Target has no unique configured executable artifact: " + name
            )
        path = artifacts[0]
        if not path.is_file():
            raise ValueError("Configured executable has not been built: " + str(path))
        return path

    def build(self, name: str, log: Path) -> None:
        self._target(name)
        log.parent.mkdir(parents=True, exist_ok=True)
        with log.open("w", encoding="utf-8") as output:
            subprocess.run(
                [
                    *self.command,
                    "--build",
                    str(self.directory),
                    "--parallel",
                    str(self.jobs),
                    "--target",
                    name,
                ],
                stdout=output,
                stderr=subprocess.STDOUT,
                timeout=self.timeout,
                check=True,
            )

    def _target(self, name):
        try:
            return self._targets[name]
        except KeyError as error:
            raise ValueError("Unknown configured CMake target: " + name) from error
