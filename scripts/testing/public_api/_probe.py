"""Explicit component-owned consumer probes, independent of repository layout."""

from dataclasses import dataclass
import json
from pathlib import Path, PurePosixPath
import re


def _headers(values):
    result = tuple(values)
    if (
        not result
        or isinstance(values, str)
        or any(
            not isinstance(value, str)
            or not re.fullmatch(r"[A-Za-z0-9_./-]+", value)
            or not PurePosixPath(value).parts
            or PurePosixPath(value).is_absolute()
            or ".." in PurePosixPath(value).parts
            or str(PurePosixPath(value)) != value
            for value in result
        )
        or len(result) != len(set(result))
    ):
        raise ValueError(
            "Probe headers must be distinct canonical relative include names"
        )
    return result


@dataclass(frozen=True)
class PackageProbe:
    package: str
    target: str
    components: tuple[str, ...]
    headers: tuple[str, ...]
    private_headers: tuple[str, ...]
    consumer: Path

    def __post_init__(self):
        if not re.fullmatch(
            r"[A-Za-z0-9_][A-Za-z0-9_.+-]*", self.package
        ) or not re.fullmatch(
            r"[A-Za-z0-9_][A-Za-z0-9_.+-]*(::[A-Za-z0-9_][A-Za-z0-9_.+-]*)*",
            self.target,
        ):
            raise ValueError("Probe package and target must be safe CMake names")
        components = tuple(self.components)
        if (
            not components
            or isinstance(self.components, str)
            or any(
                not isinstance(name, str)
                or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_-]*", name)
                for name in components
            )
            or len(components) != len(set(components))
        ):
            raise ValueError("Probe install components must be distinct identifiers")
        object.__setattr__(self, "components", components)
        object.__setattr__(self, "headers", _headers(self.headers))
        object.__setattr__(self, "private_headers", _headers(self.private_headers))
        consumer = self.consumer.resolve(strict=True)
        if not consumer.is_file():
            raise ValueError("Probe consumer must be a source file")
        object.__setattr__(self, "consumer", consumer)

    @classmethod
    def read(cls, manifest: Path) -> "PackageProbe":
        manifest = manifest.resolve(strict=True)
        data = json.loads(manifest.read_text(encoding="utf-8"))
        if (
            not isinstance(data, dict)
            or set(data)
            != {
                "package",
                "target",
                "components",
                "headers",
                "private_headers",
                "consumer",
            }
            or any(
                not isinstance(data[key], str)
                for key in ("package", "target", "consumer")
            )
            or any(
                not isinstance(data[key], list)
                for key in ("components", "headers", "private_headers")
            )
        ):
            raise ValueError(
                "Consumer manifest must declare package, target, components, headers, private_headers and consumer"
            )
        consumer = (manifest.parent / data.pop("consumer")).resolve(strict=True)
        if not consumer.is_relative_to(manifest.parent):
            raise ValueError("Probe consumer must remain inside its manifest directory")
        return cls(**data, consumer=consumer)
