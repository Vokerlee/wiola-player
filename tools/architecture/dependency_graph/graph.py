"""Extract explicit first-party source dependencies without executing source files."""

import ast
from dataclasses import dataclass
from pathlib import Path
import re
import posixpath

from tools.architecture.ownership.registry import OwnershipRegistry
from tools.architecture.cmake_inventory.inventory import CMakeInventory


@dataclass(frozen=True, order=True)
class DependencyEdge:
    source: str
    target: str
    origin: str
    destination: str
    kind: str = "include"

    @property
    def identity(self) -> str:
        return f"{self.kind}|{self.source}|{self.target}|{self.origin}|{self.destination}"


class SourceDependencyGraph:
    """Explicit includes/imports and Qt use; macro-dependent C++ is not fully parsed."""

    def __init__(self, root: Path, registry: OwnershipRegistry) -> None:
        self._root = root
        self._owners = {
            path: owner.identity for owner in registry.owners for path in owner.files
        }
        self._public_headers = {
            path.split("/public/", 1)[1]: path
            for path in self._owners if "/public/" in path and path.endswith(".hpp")
        }

    def _python_module(self, name: str) -> str | None:
        path = name.replace(".", "/")
        for candidate in (path + ".py", path + "/__init__.py"):
            if candidate in self._owners:
                return candidate
        return None

    def _python_imports(self, path: str, text: str) -> set[str]:
        targets = set()
        package = Path(path).parent.parts
        for node in ast.walk(ast.parse(text, filename=path)):
            if isinstance(node, ast.Import):
                names = [alias.name for alias in node.names]
            elif isinstance(node, ast.ImportFrom):
                prefix = package[:len(package) - node.level + 1] if node.level else ()
                name = ".".join((*prefix, node.module or "")).rstrip(".")
                names = [name]
                names.extend(name + "." + alias.name for alias in node.names)
            else:
                continue
            for name in names:
                target = self._python_module(name)
                if target:
                    targets.add(target)
        return targets

    def edges(self) -> tuple[DependencyEdge, ...]:
        edges = set()
        for path, origin in sorted(self._owners.items()):
            suffix = Path(path).suffix
            if suffix not in {".cpp", ".hpp", ".py"}:
                continue
            text = (self._root / path).read_text(encoding="utf-8")
            if suffix == ".py":
                targets = self._python_imports(path, text)
                for target in targets:
                    edges.add(DependencyEdge(path, target, origin, self._owners[target], "import"))
                continue
            for include in re.findall(r'^\s*#\s*include\s*[<"]([^>"\n]+)', text, re.M):
                if include.startswith("Qt") or re.match(r"Q[A-Z]", include):
                    edges.add(DependencyEdge(path, include, origin, "external.qt", "framework"))
                    continue
                candidates = [posixpath.normpath(str(Path(path).parent / include)), "src/" + include]
                public = self._public_headers.get(include)
                if public:
                    candidates.append(public)
                for target in candidates:
                    if target in self._owners:
                        edges.add(DependencyEdge(path, target, origin, self._owners[target]))
                        break
        return tuple(sorted(edges))


class ConfiguredDependencyGraph:
    """Configured target prerequisites and transitive Qt compile requirements.

    CMake's codemodel includes ordering prerequisites as well as link dependencies.
    Interface-only/external targets absent from it cannot be inferred as direct links.
    """

    def __init__(self, inventory: CMakeInventory, registry: OwnershipRegistry) -> None:
        self._inventory = inventory
        self._owners = {path: owner.identity for owner in registry.owners for path in owner.files}

    def edges(self) -> tuple[DependencyEdge, ...]:
        owners = {
            target.name: {self._owners[path] for path in target.sources if path in self._owners}
            for target in self._inventory.targets
        }
        edges = set()
        for target in self._inventory.targets:
            origins = owners[target.name]
            for dependency in target.dependencies:
                for origin in origins:
                    for destination in owners[dependency]:
                        edges.add(DependencyEdge(target.name, dependency, origin, destination, "configured-target"))
            if any(value.startswith("QT_") and value.endswith("_LIB")
                   for value in target.compile_definitions):
                for origin in origins:
                    edges.add(DependencyEdge(target.name, "Qt", origin, "external.qt", "configured-framework"))
        return tuple(sorted(edges))
