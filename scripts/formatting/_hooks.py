"""Install the owned hook without overwriting a user's regular hook file."""

from importlib.resources import files
import os
from pathlib import Path
import subprocess
import tempfile

from ._format import repository_root


def install_hook(repository: Path) -> Path:
    root = repository_root(repository)
    directory = Path(
        subprocess.check_output(
            ["git", "rev-parse", "--path-format=absolute", "--git-path", "hooks"],
            cwd=root,
            text=True,
            timeout=10,
        ).strip()
    )
    directory.mkdir(parents=True, exist_ok=True)
    destination = directory / "pre-commit"
    if destination.exists() and not destination.is_symlink():
        raise ValueError("Refusing to overwrite existing hook: " + str(destination))
    source = Path(files(__package__).joinpath("pre_commit.sh")).resolve()
    with tempfile.TemporaryDirectory(prefix=".wiola-hook-", dir=directory) as temporary:
        link = Path(temporary) / "pre-commit"
        link.symlink_to(os.path.relpath(source, directory))
        os.replace(link, destination)
    return destination
