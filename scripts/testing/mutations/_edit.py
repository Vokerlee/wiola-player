"""A checked, atomic edit with exact restoration and concurrent-edit protection."""

import hashlib
import os
from pathlib import Path
import stat
import tempfile


class SourceConflict(RuntimeError):
    """Restoring the snapshot would overwrite someone else's edit."""


class SourceMutation:
    def __init__(self, path: Path, anchor: bytes, replacement: bytes):
        self.path = path.resolve()
        self.original = self.path.read_bytes()
        if not anchor or anchor == replacement or self.original.count(anchor) != 1:
            raise ValueError(
                "Mutation requires one exact, nonempty, changing anchor: " + str(path)
            )
        self.mutant = self.original.replace(anchor, replacement, 1)
        self._mode = stat.S_IMODE(self.path.stat().st_mode)

    @property
    def original_sha256(self) -> str:
        return hashlib.sha256(self.original).hexdigest()

    @property
    def mutant_sha256(self) -> str:
        return hashlib.sha256(self.mutant).hexdigest()

    @property
    def restored(self) -> bool:
        return self.path.is_file() and self.path.read_bytes() == self.original

    def __enter__(self):
        if not self.restored:
            raise SourceConflict("Source changed before mutation: " + str(self.path))
        self._replace(self.mutant)
        return self

    def __exit__(self, kind, error, traceback):
        if not self.path.is_file() or self.path.read_bytes() != self.mutant:
            raise SourceConflict(
                "Source changed during mutation; refusing to overwrite: "
                + str(self.path)
            )
        self._replace(self.original)
        return False

    def _replace(self, content: bytes) -> None:
        # Prepare beside the source so replacement stays on the same filesystem. A failed
        # write/rename leaves the previous source intact. Fresh mtimes require a rebuild.
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(
                dir=self.path.parent, prefix=".wiola-mutation-", delete=False
            ) as output:
                temporary = Path(output.name)
                output.write(content)
            temporary.chmod(self._mode)
            os.replace(temporary, self.path)
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)
