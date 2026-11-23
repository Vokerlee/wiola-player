"""Acquire one verified Qt source tree without overwriting a caller's edited tree."""

from dataclasses import asdict, dataclass
import errno
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile
from typing import Callable


def file_digest(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


@dataclass(frozen=True)
class QtSourcePin:
    version: str
    sha256: str

    def __post_init__(self):
        if not re.fullmatch(r"6\.\d+\.\d+", self.version) or not re.fullmatch(
            r"[0-9a-f]{64}", self.sha256
        ):
            raise ValueError(
                "Qt source requires a supported version and reviewed SHA-256 pin"
            )

    @property
    def directory(self) -> str:
        return "qtbase-everywhere-src-" + self.version

    @property
    def archive(self) -> str:
        return self.directory + ".tar.xz"

    @property
    def url(self) -> str:
        series = self.version.rsplit(".", 1)[0]
        return f"https://download.qt.io/archive/qt/{series}/{self.version}/submodules/{self.archive}"

    @property
    def identity(self) -> str:
        owner = Path(__file__).parent
        identity = hashlib.sha256(
            json.dumps(asdict(self), sort_keys=True).encode("utf-8")
        )
        for path in sorted(owner.glob("*.py")):
            identity.update(path.name.encode("utf-8") + b"\0" + path.read_bytes())
        return identity.hexdigest()

    @classmethod
    def resolve(cls, version: str) -> "QtSourcePin":
        pins = json.loads(
            Path(__file__).with_name("pins.json").read_text(encoding="utf-8")
        )
        if version not in pins:
            raise ValueError(
                "No reviewed Qt source pin for "
                + version
                + "; add a verified release to qt_source/pins.json"
            )
        return cls(version, pins[version])


class QtSourceStore:
    def __init__(self, downloader: Callable[[str, Path], None] | None = None):
        self._download = downloader or self._curl

    def materialize(self, pin: QtSourcePin, workspace: Path) -> Path:
        workspace = workspace.resolve()
        workspace.mkdir(parents=True, exist_ok=True)
        source = workspace / pin.directory
        receipt = workspace / (pin.directory + ".json")
        expected = {"schema_version": 1, "source_identity": pin.identity}
        try:
            cached = json.loads(receipt.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            cached = None
        if (
            isinstance(cached, dict)
            and cached.get("source_identity") == pin.identity
            and source.is_dir()
        ):
            if cached != dict(expected, tree_sha256=self._tree_digest(source)):
                raise ValueError(
                    "Cached Qt source was edited; select a clean workspace: "
                    + str(source)
                )
            return source

        archive = self._archive(pin, workspace)
        with tempfile.TemporaryDirectory(
            prefix=".qt-source-", dir=workspace
        ) as staging:
            with tarfile.open(archive) as package:
                package.extractall(staging, filter="data")
            unpacked = Path(staging) / pin.directory
            if not (unpacked / "CMakeLists.txt").is_file():
                raise ValueError(
                    "Verified Qt archive has no expected source entrypoint"
                )
            tree_identity = self._tree_digest(unpacked)
            if source.exists():
                self._require_same_tree(source, tree_identity)
            else:
                try:
                    unpacked.rename(source)
                except OSError as error:
                    # Another user of this same source store may have published the same
                    # verified tree. Accept identical contents, never replace an edited tree.
                    if error.errno not in {errno.EEXIST, errno.ENOTEMPTY}:
                        raise
                    self._require_same_tree(source, tree_identity)
            self._write_receipt(receipt, dict(expected, tree_sha256=tree_identity))
        return source

    def _archive(self, pin: QtSourcePin, workspace: Path) -> Path:
        download = workspace / "download"
        download.mkdir(parents=True, exist_ok=True)
        archive = download / pin.archive
        if archive.is_file():
            if file_digest(archive) != pin.sha256:
                raise ValueError("Cached Qt archive checksum mismatch: " + str(archive))
            return archive
        with tempfile.TemporaryDirectory(
            prefix=".qt-download-", dir=download
        ) as staging:
            temporary = Path(staging) / pin.archive
            self._download(pin.url, temporary)
            if file_digest(temporary) != pin.sha256:
                raise ValueError("Downloaded Qt archive checksum mismatch")
            os.replace(temporary, archive)
        return archive

    @staticmethod
    def _curl(url: str, destination: Path) -> None:
        subprocess.run(
            [
                "curl",
                "--fail",
                "--location",
                "--retry",
                "3",
                "--connect-timeout",
                "30",
                "--speed-time",
                "30",
                "--speed-limit",
                "1024",
                url,
                "-o",
                str(destination),
            ],
            check=True,
        )

    @staticmethod
    def _tree_digest(root: Path) -> str:
        result = hashlib.sha256()
        for path in sorted(root.rglob("*")):
            name = path.relative_to(root).as_posix().encode("utf-8")
            if path.is_symlink():
                result.update(
                    b"link\0" + name + b"\0" + os.readlink(path).encode("utf-8")
                )
            elif path.is_file():
                executable = str(path.stat().st_mode & 0o111).encode("ascii")
                result.update(
                    b"file\0"
                    + name
                    + b"\0"
                    + executable
                    + b"\0"
                    + file_digest(path).encode("ascii")
                )
            elif path.is_dir():
                result.update(b"dir\0" + name + b"\0")
            else:
                raise ValueError("Unsupported entry in Qt source cache: " + str(path))
        return result.hexdigest()

    @classmethod
    def _require_same_tree(cls, source: Path, expected: str) -> None:
        if not source.is_dir() or cls._tree_digest(source) != expected:
            raise ValueError(
                "Existing Qt source differs from the reviewed archive; select a clean workspace: "
                + str(source)
            )

    @staticmethod
    def _write_receipt(path: Path, value: dict) -> None:
        with tempfile.TemporaryDirectory(
            prefix=".qt-receipt-", dir=path.parent
        ) as staging:
            temporary = Path(staging) / path.name
            temporary.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
            os.replace(temporary, path)
