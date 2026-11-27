"""Normalize the C++ file prelude; clang-format owns include grouping."""

from pathlib import Path
import re


_WHITESPACE = re.compile(r"\s*")
_PRAGMA = re.compile(r"(?P<pragma>\#[ \t]*pragma[ \t]+once[ \t]*(?://[^\n]*)?)(?:\n|$)")


def spaced_pragma_once(source: str) -> str:
    position = 0

    while True:
        position = _WHITESPACE.match(source, position).end()

        if source.startswith("//", position):
            end = source.find("\n", position + 2)

            if end < 0:
                return source

            position = end + 1
        elif source.startswith("/*", position):
            end = source.find("*/", position + 2)

            if end < 0:
                return source

            position = end + 2
        else:
            break

    match = _PRAGMA.match(source, position)

    if match is None:
        return source

    prefix = source[:position].rstrip()
    pragma = match["pragma"].rstrip()
    body = source[match.end():].lstrip(" \t\r\n")

    return (prefix + "\n\n" if prefix else "") + pragma + "\n" + (
        "\n" + body if body else ""
    )


def format_cpp_preludes(paths: list[str], *, check: bool) -> None:
    changes = []

    for name in paths:
        path = Path(name)
        source = path.read_text(encoding="utf-8")
        formatted = spaced_pragma_once(source)

        if source != formatted:
            changes.append((path, formatted))

    if check and changes:
        raise ValueError(
            "Separate #pragma once from adjacent comments and includes with a blank line: "
            + ", ".join(str(path) for path, _ in changes)
        )

    for path, formatted in changes:
        path.write_text(formatted, encoding="utf-8")
