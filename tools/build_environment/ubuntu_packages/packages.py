"""APT owns dependency resolution and archive verification; callers own cache transport."""

from dataclasses import asdict, dataclass
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile


@dataclass(frozen=True, order=True)
class PackageArchive:
    filename: str
    size: int
    checksum: str

    def __post_init__(self):
        if (
            not re.fullmatch(r"[A-Za-z0-9+_.%~:-]+\.deb", self.filename)
            or type(self.size) is not int
            or self.size <= 0
            or not re.fullmatch(r"[A-Za-z0-9]+:[0-9a-fA-F]+", self.checksum)
        ):
            raise ValueError("Invalid APT archive identity")


@dataclass(frozen=True)
class PackagePlan:
    packages: tuple[str, ...]
    archives: tuple[PackageArchive, ...]

    def __post_init__(self):
        if not self.packages or any(
            not re.fullmatch(r"[a-z0-9][a-z0-9+.-]*(?::[a-z0-9-]+)?(?:=[^\s]+)?", p)
            for p in self.packages
        ):
            raise ValueError("Expected explicit APT package names or version requests")
        if len({a.filename for a in self.archives}) != len(self.archives):
            raise ValueError("Duplicate archive in APT plan")

    def serialize(self) -> str:
        return json.dumps(asdict(self), sort_keys=True, separators=(",", ":"))

    @property
    def identity(self) -> str:
        return hashlib.sha256(self.serialize().encode("utf-8")).hexdigest()

    @classmethod
    def read(cls, path: Path) -> "PackagePlan":
        data = json.loads(path.read_text())
        return cls(
            tuple(data["packages"]),
            tuple(PackageArchive(**a) for a in data["archives"]),
        )

    @classmethod
    def from_uris(cls, packages: tuple[str, ...], output: str) -> "PackagePlan":
        archives = []
        for line in output.splitlines():
            if not line.startswith("'"):
                continue
            fields = shlex.split(line)
            if len(fields) != 4:
                raise ValueError("Unexpected APT archive description")
            # Mirror URLs do not change the identity of identical package bytes.
            archives.append(PackageArchive(fields[1], int(fields[2]), fields[3]))
        return cls(tuple(sorted(set(packages))), tuple(sorted(archives)))


class UbuntuPackages:
    """Prepare an APT transaction and retain only its reusable package archives."""

    def __init__(
        self, command=("sudo", "env", "DEBIAN_FRONTEND=noninteractive", "apt-get")
    ):
        self.command = (
            *command,
            "-o",
            "Acquire::Retries=3",
            "-o",
            "Acquire::http::Timeout=30",
            "-o",
            "Acquire::https::Timeout=30",
        )

    def resolve(self, packages: tuple[str, ...]) -> PackagePlan:
        PackagePlan(
            packages, ()
        )  # Reject options before invoking a privileged command.
        subprocess.run([*self.command, "update"], check=True, stdout=sys.stderr)
        # A populated archive directory would suppress URI lines and change the cache key.
        with tempfile.TemporaryDirectory(prefix="wiola-apt-plan-") as directory:
            result = subprocess.run(
                [
                    *self.command,
                    "-o",
                    f"Dir::Cache::archives={directory}",
                    "--print-uris",
                    "--yes",
                    "--no-install-recommends",
                    "install",
                    "--",
                    *packages,
                ],
                check=True,
                text=True,
                stdout=subprocess.PIPE,
            )
        return PackagePlan.from_uris(packages, result.stdout)

    def install(self, plan: PackagePlan, archives: Path) -> None:
        archives = archives.resolve()
        archives.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            [
                *self.command,
                "-o",
                f"Dir::Cache::archives={archives}",
                "-o",
                "APT::Keep-Downloaded-Packages=true",
                "-o",
                "Binary::apt-get::APT::Keep-Downloaded-Packages=true",
                "--yes",
                "--no-install-recommends",
                "install",
                "--",
                *plan.packages,
            ],
            check=True,
        )
        retained = {archive.filename for archive in plan.archives}
        for path in archives.glob("*.deb"):
            if path.name not in retained:
                path.unlink()
