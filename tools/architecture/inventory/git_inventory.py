"""Read tracked paths without consulting untracked plans or build products."""

from pathlib import Path
import subprocess


class GitInventory:
    """Git's index is the authoritative set of repository-owned files."""

    def __init__(self, repository: Path) -> None:
        self._repository = repository

    def paths(self) -> tuple[str, ...]:
        result = subprocess.run(
            ["git", "ls-files", "--full-name", "-z", "--", ":/"],
            cwd=self._repository,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=10,
        )
        return tuple(
            path.decode("utf-8") for path in result.stdout.split(b"\0") if path
        )
