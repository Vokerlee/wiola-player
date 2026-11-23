import json
from pathlib import Path
import tempfile
import unittest

from tools.architecture.cmake_inventory.inventory import BuildTarget, CMakeInventory
from tools.architecture.dependency_graph.graph import SourceDependencyGraph, ConfiguredDependencyGraph
from tools.architecture.ownership.registry import OwnershipRegistry


class DependencyGraphTest(unittest.TestCase):
    def test_relative_public_and_root_includes_have_explicit_owners(self):
        documents = {"src/a/main.cpp": '#include "../b/private.hpp"\n#include <b/public.hpp>\n#include <QtCore/QObject>\n',
                     "src/b/private.hpp": "", "src/b/public/b/public.hpp": ""}
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            owners = []
            for index, (path, content) in enumerate(documents.items()):
                file = root / path
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_text(content)
                owners.append({"id": "fixture." + str(index), "purpose": "Fixture", "files": [path]})
            registry = OwnershipRegistry([json.dumps({"schema_version": 1, "owners": owners})])
            edges = SourceDependencyGraph(root, registry).edges()
            self.assertEqual({(e.target, e.destination) for e in edges}, {
                ("src/b/private.hpp", "fixture.1"), ("src/b/public/b/public.hpp", "fixture.2"),
                ("QtCore/QObject", "external.qt")})

    def test_python_relative_imports_are_observed_without_execution(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            paths = {"package/reader.py": "from .private import value\nraise RuntimeError('must not run')\n",
                     "package/private.py": "value = 1\n"}
            for path, content in paths.items():
                file = root / path
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_text(content)
            registry = OwnershipRegistry([json.dumps({"schema_version": 1, "owners": [
                {"id": "reader", "purpose": "Reader", "files": ["package/reader.py"]},
                {"id": "writer", "purpose": "Writer", "files": ["package/private.py"]}]})])
            edge, = SourceDependencyGraph(root, registry).edges()
            self.assertEqual((edge.origin, edge.destination, edge.kind), ("reader", "writer", "import"))

    def test_configured_prerequisites_and_transitive_qt_usage_are_not_silent(self):
        registry = OwnershipRegistry([json.dumps({"schema_version": 1, "owners": [
            {"id": "engine", "purpose": "Engine", "files": ["src/engine.cpp"]},
            {"id": "app", "purpose": "App", "files": ["src/app.cpp"]}]})])
        inventory = CMakeInventory(Path("."), (
            BuildTarget("runtime", "STATIC_LIBRARY", ("src/engine.cpp",), (), ("application",), (), ("QT_CORE_LIB",)),
            BuildTarget("application", "STATIC_LIBRARY", ("src/app.cpp",), (), (), ())))
        edges = ConfiguredDependencyGraph(inventory, registry).edges()
        self.assertEqual({(e.origin, e.destination, e.kind) for e in edges}, {
            ("engine", "app", "configured-target"), ("engine", "external.qt", "configured-framework")})
