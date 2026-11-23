"""Atomic publication and qualification of the generated Qt Core runtime."""

import json
import os
from pathlib import Path
import shutil
import tempfile
import re
import subprocess
from typing import Mapping

from .context import digest


class QtCoreRuntime:
    def __init__(self, directory: Path):
        self.directory = directory.resolve()
        self.library = self.directory / "libQt6Core.so.6"
        self.receipt = self.directory / "receipt.json"

    def qualified(self, identity: str) -> bool:
        try:
            expected = {
                "schema_version": 1,
                "context": identity,
                "library_sha256": digest(self.library),
            }
            value = json.loads(self.receipt.read_text(encoding="utf-8"))
            return (
                isinstance(value, dict)
                and type(value.get("schema_version")) is int
                and value == expected
            )
        except (OSError, ValueError):
            return False

    def publish(self, library: Path, identity: str) -> None:
        self.directory.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(
            prefix=".qt-runtime-", dir=self.directory
        ) as staging:
            temporary = Path(staging) / self.library.name
            shutil.copy2(library, temporary)
            os.replace(temporary, self.library)
            value = {
                "schema_version": 1,
                "context": identity,
                "library_sha256": digest(self.library),
            }
            receipt = Path(staging) / self.receipt.name
            receipt.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
            os.replace(receipt, self.receipt)

    def verify_loaded(
        self,
        binary: Path,
        log: Path,
        identity: str,
        *,
        environment: Mapping[str, str] | None = None,
    ) -> None:
        if not self.qualified(identity):
            raise ValueError(
                "Qt runtime is not qualified for the selected build context"
            )
        result = subprocess.run(
            ["ldd", str(binary.resolve())],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
            timeout=30,
            env=dict(os.environ) if environment is None else dict(environment),
        )
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text(result.stdout, encoding="utf-8")
        if result.returncode:
            raise ValueError("Cannot inspect program runtime; see " + str(log))
        pattern = (
            r"^\s*"
            + re.escape(self.library.name)
            + r"\s+=>\s+(.*?)\s+\(0x[0-9a-fA-F]+\)\s*$"
        )
        paths = [
            Path(match.group(1)).resolve()
            for line in result.stdout.splitlines()
            if (match := re.fullmatch(pattern, line))
        ]
        if paths != [self.library.resolve()]:
            raise ValueError(
                "Program did not resolve the qualified instrumented Core; see "
                + str(log)
            )
