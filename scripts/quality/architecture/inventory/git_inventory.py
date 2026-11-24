"""Read tracked paths without consulting untracked plans or build products."""

from pathlib import Path, PurePosixPath
import subprocess


class GitInventory:
    """Git's index is the authoritative set of repository-owned files."""

    def __init__(self, repository: Path) -> None:
        self._repository = repository

    @property
    def root(self) -> Path:
        """The actual owning worktree, including when the caller supplies a subfolder."""
        return Path(
            subprocess.check_output(
                ["git", "rev-parse", "--show-toplevel"],
                cwd=self._repository,
                text=True,
                stderr=subprocess.PIPE,
                timeout=10,
            ).strip()
        ).resolve()

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
            root = self.root
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

    def read_previous(self, path: str, reference: str) -> str:
        """Read trusted bytes, following only Git-proven committed renames if needed."""
        revision = self.revision(reference)
        try:
            return self.read(path, revision)
        except subprocess.CalledProcessError:
            names = (
                subprocess.check_output(
                    [
                        "git",
                        "diff",
                        "--no-relative",
                        "--name-status",
                        "-z",
                        "--find-renames",
                        "--diff-filter=R",
                        revision,
                        "HEAD",
                        "--",
                    ],
                    cwd=self._repository,
                    stderr=subprocess.PIPE,
                    timeout=10,
                )
                .decode("utf-8")
                .split("\0")[:-1]
            )
            for _, previous, current in zip(
                names[::3], names[1::3], names[2::3], strict=True
            ):
                if current == path:
                    return self.read(previous, revision)
            # A newly added/deleted/unmatched path is not trusted prior policy.
            # Preserve the original Git error; never substitute current worktree bytes.
            raise

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
