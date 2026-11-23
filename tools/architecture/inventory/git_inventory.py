"""Read tracked paths without consulting untracked plans or build products."""

from pathlib import Path, PurePosixPath
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

    def revision(self, reference: str) -> str:
        return subprocess.check_output(
            [
                "git",
                "rev-parse",
                "--verify",
                "--end-of-options",
                reference + "^{commit}",
            ],
            cwd=self._repository,
            text=True,
            stderr=subprocess.PIPE,
            timeout=10,
        ).strip()

    def read(self, path: str, reference: str | None = None) -> str:
        candidate = PurePosixPath(path)
        if (
            candidate.is_absolute()
            or ".." in candidate.parts
            or str(candidate) != path
            or "\\" in path
        ):
            raise ValueError("Git paths must be canonical repository-relative paths")
        if reference is None:
            root = Path(
                subprocess.check_output(
                    ["git", "rev-parse", "--show-toplevel"],
                    cwd=self._repository,
                    text=True,
                    stderr=subprocess.PIPE,
                    timeout=10,
                ).strip()
            ).resolve()
            file = (root / path).resolve()
            if not file.is_relative_to(root):
                raise ValueError("Worktree source escapes the repository")
            return file.read_text(encoding="utf-8")
        revision = self.revision(reference)
        return subprocess.check_output(
            ["git", "show", revision + ":" + path],
            cwd=self._repository,
            text=True,
            stderr=subprocess.PIPE,
            timeout=10,
        )

    def deleted_paths(self, reference: str) -> tuple[str, ...]:
        revision = self.revision(reference)
        result = subprocess.check_output(
            [
                "git",
                "diff",
                "--name-only",
                "--diff-filter=D",
                "-z",
                revision,
                "HEAD",
                "--",
            ],
            cwd=self._repository,
            stderr=subprocess.PIPE,
            timeout=10,
        )
        return tuple(path.decode("utf-8") for path in result.split(b"\0") if path)

    def commit_message(self) -> str:
        return subprocess.check_output(
            ["git", "show", "-s", "--format=%B", "HEAD"],
            cwd=self._repository,
            text=True,
            stderr=subprocess.PIPE,
            timeout=10,
        ).strip()
