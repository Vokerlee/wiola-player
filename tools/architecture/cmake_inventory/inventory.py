"""Read declared source, target, artifact and dependency ownership from CMake."""

from dataclasses import dataclass
from functools import cache
from typing import Iterable
import json
from pathlib import Path


class BuildInventoryError(ValueError):
    """The configured build does not provide a usable codemodel reply."""


@dataclass(frozen=True)
class BuildTarget:
    name: str
    kind: str
    sources: tuple[str, ...]
    generated_sources: tuple[str, ...]
    dependencies: tuple[str, ...]
    artifacts: tuple[Path, ...]
    compile_definitions: tuple[str, ...] = ()


@dataclass(frozen=True)
class CMakeInventory:
    source_root: Path
    targets: tuple[BuildTarget, ...]

    @classmethod
    def read(
        cls, build_directory: Path, *, first_party_sources: Iterable[str] = ()
    ) -> "CMakeInventory":
        build = build_directory.resolve()
        declared = frozenset(first_party_sources)
        reply = build / ".cmake" / "api" / "v1" / "reply"

        def read_reference(name: str) -> dict:
            if (
                not isinstance(name, str)
                or Path(name).name != name
                or not name.endswith(".json")
            ):
                raise BuildInventoryError(
                    "CMake reply references must be local JSON filenames"
                )
            value = json.loads((reply / name).read_text(encoding="utf-8"))
            if not isinstance(value, dict):
                raise BuildInventoryError(f"CMake reply must contain an object: {name}")
            return value

        try:
            indexes = sorted(reply.glob("index-*.json"))
            if not indexes:
                raise BuildInventoryError(
                    "Configure a CMake File API codemodel-v2 query first"
                )
            index = read_reference(indexes[-1].name)
            model = read_reference(index["reply"]["codemodel-v2"]["jsonFile"])
            if model["version"]["major"] != 2 or len(model["configurations"]) != 1:
                raise BuildInventoryError(
                    "Select one configured codemodel-v2 build variant"
                )
            source_root = Path(model["paths"]["source"]).resolve()

            @cache
            def inside_build_tree(directory: Path) -> bool:
                if directory == source_root or not directory.is_relative_to(
                    source_root
                ):
                    return False
                return (directory / "CMakeCache.txt").is_file() or inside_build_tree(
                    directory.parent
                )

            entries = model["configurations"][0]["targets"]
            names = {entry["id"]: entry["name"] for entry in entries}
            if len(names) != len(entries) or len(set(names.values())) != len(entries):
                raise BuildInventoryError("CMake target identities must be unique")
            targets = []
            for entry in entries:
                target = read_reference(entry["jsonFile"])
                sources, generated = [], []
                for item in target.get("sources", []):
                    path = Path(item["path"])
                    if not path.is_absolute():
                        path = source_root / path
                    path = path.resolve()
                    if not path.is_relative_to(source_root):
                        continue
                    relative = path.relative_to(source_root).as_posix()
                    in_active_build = build.is_relative_to(
                        source_root
                    ) and path.is_relative_to(build)
                    if relative not in declared and (
                        in_active_build or inside_build_tree(path.parent)
                    ):
                        if not item.get("isGenerated", False) or not in_active_build:
                            continue
                    (generated if item.get("isGenerated", False) else sources).append(
                        relative
                    )
                artifacts = tuple(
                    (build / artifact["path"]).resolve()
                    for artifact in target.get("artifacts", [])
                )
                targets.append(
                    BuildTarget(
                        entry["name"],
                        target["type"],
                        tuple(sorted(set(sources))),
                        tuple(sorted(set(generated))),
                        tuple(
                            sorted(
                                names[item["id"]]
                                for item in target.get("dependencies", [])
                            )
                        ),
                        artifacts,
                        tuple(
                            sorted(
                                {
                                    definition["define"]
                                    for group in target.get("compileGroups", [])
                                    for definition in group.get("defines", [])
                                }
                            )
                        ),
                    )
                )
            return cls(
                source_root, tuple(sorted(targets, key=lambda target: target.name))
            )
        except (KeyError, TypeError, OSError, json.JSONDecodeError) as error:
            raise BuildInventoryError(
                f"Invalid or incomplete CMake codemodel: {error}"
            ) from error
