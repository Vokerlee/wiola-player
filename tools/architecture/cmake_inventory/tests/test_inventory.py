import json
from pathlib import Path
import tempfile
import unittest

from tools.architecture.cmake_inventory.inventory import CMakeInventory, BuildInventoryError


class CMakeInventoryTest(unittest.TestCase):
    def write(self, directory, name, value):
        path = directory / ".cmake/api/v1/reply" / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value))

    def model(self, directory):
        source = directory / "repository"
        self.write(directory, "index-001.json", {
            "reply": {"codemodel-v2": {"jsonFile": "model.json"}}})
        model = {"version": {"major": 2}, "paths": {"source": str(source)},
                 "configurations": [{"targets": [
                     {"id": "app-id", "name": "player", "jsonFile": "player.json"},
                     {"id": "core-id", "name": "core", "jsonFile": "core.json"}]}]}
        self.write(directory, "model.json", model)
        self.write(directory, "core.json", {"type": "STATIC_LIBRARY", "sources": [
            {"path": "src/core.cpp"}, {"path": "generated/moc.cpp", "isGenerated": True},
            {"path": str(directory / "external/foreign.cpp")}],
            "artifacts": [{"path": "lib/core.a"}]})
        self.write(directory, "player.json", {"type": "EXECUTABLE", "sources": [
            {"path": "src/main.cpp"}], "dependencies": [{"id": "core-id"}],
            "artifacts": [{"path": "bin/player"}]})
        return source, model

    def test_resolves_actual_targets_sources_dependencies_and_artifacts(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            source, _ = self.model(directory)
            inventory = CMakeInventory.read(directory)
            self.assertEqual(inventory.source_root, source)
            core, player = inventory.targets
            self.assertEqual(core.name, "core")
            self.assertEqual(core.sources, ("src/core.cpp",))
            self.assertEqual(core.generated_sources, ("generated/moc.cpp",))
            self.assertEqual(player.dependencies, ("core",))
            self.assertEqual(player.artifacts, (directory / "bin/player",))

    def test_missing_reply_and_invalid_reference_fail_explicitly(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            with self.assertRaises(BuildInventoryError):
                CMakeInventory.read(directory)
            self.write(directory, "index-001.json", {
                "reply": {"codemodel-v2": {"jsonFile": "../outside.json"}}})
            with self.assertRaises(BuildInventoryError):
                CMakeInventory.read(directory)

    def test_unsupported_or_ambiguous_models_are_not_silently_selected(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            _, model = self.model(directory)
            for version, configurations in [(3, model["configurations"]), (2, []),
                                             (2, model["configurations"] * 2)]:
                self.write(directory, "model.json", dict(model,
                    version={"major": version}, configurations=configurations))
                with self.assertRaises(BuildInventoryError):
                    CMakeInventory.read(directory)

    def test_unknown_dependencies_and_duplicate_target_ids_are_rejected(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            _, model = self.model(directory)
            self.write(directory, "player.json", {"type": "EXECUTABLE",
                "dependencies": [{"id": "unknown-id"}]})
            with self.assertRaises(BuildInventoryError):
                CMakeInventory.read(directory)
            entries = model["configurations"][0]["targets"]
            entries[1]["id"] = entries[0]["id"]
            self.write(directory, "model.json", model)
            with self.assertRaises(BuildInventoryError):
                CMakeInventory.read(directory)


if __name__ == "__main__":
    unittest.main()
