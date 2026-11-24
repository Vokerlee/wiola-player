"""One immutable specification of supported formatters and tracked input types."""

from dataclasses import dataclass
from types import MappingProxyType


@dataclass(frozen=True)
class FormatRule:
    command: str
    patterns: tuple[str, ...]
    check: tuple[str, ...]
    write: tuple[str, ...]


RULES = MappingProxyType(
    {
        "cmake": FormatRule(
            "gersemi",
            ("*CMakeLists.txt", "*CMakeLists.txt.in", "*.cmake", "*.cmake.in"),
            ("--check",),
            ("-i",),
        ),
        "cpp": FormatRule(
            "clang-format",
            ("*.c", "*.cc", "*.cpp", "*.cxx", "*.h", "*.hh", "*.hpp", "*.hxx", "*.ipp"),
            ("--dry-run", "--Werror"),
            ("-i",),
        ),
    }
)
