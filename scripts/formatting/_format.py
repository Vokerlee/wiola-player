"""Format only tracked source inputs through explicit tool protocols."""

from fnmatch import fnmatchcase
from pathlib import Path
import subprocess

from ._cpp import format_cpp_preludes
from ._rules import RULES


def repository_root(repository: Path) -> Path:
    return Path(
        subprocess.check_output(
            ["git", "rev-parse", "--show-toplevel"],
            cwd=repository,
            text=True,
            timeout=10,
        ).strip()
    ).resolve()


def format_sources(
    repository: Path, languages=("cmake", "cpp"), *, check=False, paths=()
) -> None:
    languages = tuple(dict.fromkeys(languages))

    if not languages or any(name not in RULES for name in languages):
        raise ValueError("Select at least one supported formatter")

    root = repository_root(repository)
    tracked = set(
        subprocess.check_output(["git", "ls-files", "-z"], cwd=root, timeout=10)
        .decode("utf-8")
        .split("\0")
    )
    tracked.discard("")
    selected = {}

    for item in paths or sorted(tracked):
        candidate = root / item
        name = candidate.relative_to(root).as_posix()
        matches = any(
            fnmatchcase(name, pattern)
            for language in languages
            for pattern in RULES[language].patterns
        )

        if not paths and not matches:
            continue

        if not candidate.resolve().is_relative_to(root):
            raise ValueError("Format input escapes the repository: " + str(item))

        if paths and (name not in tracked or not candidate.is_file() or not matches):
            raise ValueError(
                "Explicit format input must be an existing tracked source: " + str(item)
            )

        if candidate.is_file() and matches:
            selected[name] = candidate

    for language in languages:
        rule = RULES[language]
        files = [
            str(path)
            for name, path in selected.items()
            if any(fnmatchcase(name, pattern) for pattern in rule.patterns)
        ]

        if files:
            if language == "cpp":
                format_cpp_preludes(files, check=check)

            subprocess.run(
                [rule.command, *(rule.check if check else rule.write), *files],
                cwd=root,
                check=True,
            )
