"""Require reviewable change contracts and the repository's commit naming convention."""

from dataclasses import dataclass
import re

SECTIONS = (
    "Responsibility",
    "Dependencies",
    "Lifetime and errors",
    "Removed code",
    "Validation",
)


@dataclass(frozen=True)
class ChangeReview:
    responsibility: str
    dependencies: str
    lifetime_errors: str
    removed: str
    validation: str

    @classmethod
    def read(cls, markdown: str) -> "ChangeReview":
        text = re.sub(r"<!--.*?-->", "", markdown, flags=re.S).replace("\r\n", "\n")
        matches = list(re.finditer(r"^## ([^\n]+)\n", text, re.M))
        fields = {}
        for index, match in enumerate(matches):
            heading = match.group(1).strip()
            if heading not in SECTIONS:
                continue
            if heading in fields:
                raise ValueError("Duplicate review section: " + heading)
            end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
            fields[heading] = text[match.end() : end].strip()
        for heading in SECTIONS:
            value = fields.get(heading, "")
            if len(value) < 12 or re.match(r"(?:TODO|TBD)\b|<describe", value, re.I):
                raise ValueError("Missing concrete review section: " + heading)
        return cls(*(fields[heading] for heading in SECTIONS))

    def reviewed_deletions(self, deleted_paths: tuple[str, ...]) -> frozenset[str]:
        return frozenset(path for path in deleted_paths if f"`{path}`" in self.removed)


def validate_subject(subject: str, *, published: bool) -> None:
    suffix = r" \(#[1-9][0-9]*\)" if published else r"(?: \(#[1-9][0-9]*\))?"
    if not re.fullmatch(r"[a-z][a-z0-9_-]*: [a-z][^\r\n]+" + suffix, subject):
        raise ValueError(
            "Commit/PR subject must use area: lower-case description, with (#PR) on main"
        )
    description = re.sub(r" \(#[0-9]+\)$", "", subject).split(": ", 1)[1]
    if description.endswith("."):
        raise ValueError("Commit/PR subjects have no final full stop")
