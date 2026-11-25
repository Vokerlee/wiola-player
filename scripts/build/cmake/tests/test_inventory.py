import json
from pathlib import Path
import tempfile
import unittest

from scripts.build.cmake import (
    CMakeInventory,
    BuildInventoryError,
)


class CMakeInventoryTest(unittest.TestCase):
    def write(self, directory, name, value):
        path = directory / ".cmake/api/v1/reply" / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value))

    def model(self, directory):
        source = directory / "repository"
        self.write(
            directory,
            "index-001.json",
            {"reply": {"codemodel-v2": {"jsonFile": "model.json"}}},
        )
        model = {
            "version": {"major": 2},
            "paths": {"source": str(source)},
            "configurations": [
                {
                    "targets": [
                        {"id": "app-id", "name": "player", "jsonFile": "player.json"},
                        {"id": "core-id", "name": "core", "jsonFile": "core.json"},
                    ]
                }
            ],
        }
        self.write(directory, "model.json", model)
        self.write(
            directory,
            "core.json",
            {
                "type": "STATIC_LIBRARY",
                "sources": [
                    {"path": "src/core.cpp"},
                    {"path": "generated/moc.cpp", "isGenerated": True},
                    {"path": str(directory / "external/foreign.cpp")},
                ],
                "artifacts": [{"path": "lib/core.a"}],
                "compileGroups": [{"defines": [{"define": "QT_CORE_LIB"}]}],
            },
        )
        self.write(
            directory,
            "player.json",
            {
                "type": "EXECUTABLE",
                "nameOnDisk": "player",
                "sources": [{"path": "src/main.cpp"}],
                "dependencies": [{"id": "core-id"}],
                "artifacts": [{"path": "bin/player"}],
            },
        )
        return source, model

    def test_resolves_actual_targets_sources_dependencies_and_artifacts(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            source, _ = self.model(directory)
            inventory = CMakeInventory.read(directory)
            self.assertEqual(inventory.source_root, source)
            self.assertEqual(inventory.build_directory, directory.resolve())
            core, player = inventory.targets
            self.assertEqual(core.name, "core")
            self.assertEqual(core.compile_definitions, ("QT_CORE_LIB",))
            self.assertEqual(core.sources, ("src/core.cpp",))
            self.assertEqual(core.generated_sources, ("generated/moc.cpp",))
            self.assertEqual(player.dependencies, ("core",))
            self.assertEqual(player.name_on_disk, "player")
            self.assertEqual(player.artifacts, (directory / "bin/player",))

    def test_malformed_primary_artifact_name_is_rejected(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            self.model(directory)
            for value in ("", 17):
                self.write(
                    directory,
                    "player.json",
                    {"type": "EXECUTABLE", "nameOnDisk": value},
                )
                with self.assertRaisesRegex(BuildInventoryError, "primary artifact name"):
                    CMakeInventory.read(directory)

    def test_missing_reply_and_invalid_reference_fail_explicitly(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            with self.assertRaises(BuildInventoryError):
                CMakeInventory.read(directory)
            self.write(
                directory,
                "index-001.json",
                {"reply": {"codemodel-v2": {"jsonFile": "../outside.json"}}},
            )
            with self.assertRaises(BuildInventoryError):
                CMakeInventory.read(directory)

    def test_valid_json_that_is_not_an_object_is_not_a_codemodel(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            self.model(directory)
            self.write(directory, "model.json", [])
            with self.assertRaisesRegex(BuildInventoryError, "must contain an object"):
                CMakeInventory.read(directory)

    def test_only_active_build_generated_sources_are_retained(self):
        with tempfile.TemporaryDirectory() as name:
            repository = Path(name) / "repository"
            directory = repository / "nonstandard-output"
            _, model = self.model(directory)
            model["paths"]["source"] = str(repository)
            self.write(directory, "model.json", model)
            foreign = repository / "other-output"
            foreign.mkdir(parents=True)
            (foreign / "CMakeCache.txt").write_text("# another configured build")
            self.write(
                directory,
                "core.json",
                {
                    "type": "STATIC_LIBRARY",
                    "sources": [
                        {"path": "src/core.cpp"},
                        {"path": str(directory / "generated.cpp"), "isGenerated": True},
                        {"path": str(directory / "unowned.cpp")},
                        {"path": str(foreign / "generated.cpp"), "isGenerated": True},
                    ],
                },
            )

            inventory = CMakeInventory.read(directory)

            core = inventory.targets[0]
            self.assertEqual(core.sources, ("src/core.cpp",))
            self.assertEqual(
                core.generated_sources, ("nonstandard-output/generated.cpp",)
            )

    def test_unsupported_or_ambiguous_models_are_not_silently_selected(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            _, model = self.model(directory)
            for version, configurations in [
                (3, model["configurations"]),
                (2, []),
                (2, model["configurations"] * 2),
            ]:
                self.write(
                    directory,
                    "model.json",
                    dict(
                        model, version={"major": version}, configurations=configurations
                    ),
                )
                with self.assertRaises(BuildInventoryError):
                    CMakeInventory.read(directory)

    def test_unknown_dependencies_and_duplicate_target_ids_are_rejected(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            _, model = self.model(directory)
            self.write(
                directory,
                "player.json",
                {"type": "EXECUTABLE", "dependencies": [{"id": "unknown-id"}]},
            )
            with self.assertRaises(BuildInventoryError):
                CMakeInventory.read(directory)
            entries = model["configurations"][0]["targets"]
            entries[1]["id"] = entries[0]["id"]
            self.write(directory, "model.json", model)
            with self.assertRaises(BuildInventoryError):
                CMakeInventory.read(directory)

    def test_external_sources_in_another_configured_build_are_not_first_party(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            source, _ = self.model(directory)
            foreign = source / "unusual-output-location"
            foreign.mkdir(parents=True)
            (foreign / "CMakeCache.txt").write_text("# another configured build")
            self.write(
                directory,
                "core.json",
                {
                    "type": "STATIC_LIBRARY",
                    "sources": [
                        {"path": "src/core.cpp"},
                        {"path": "unusual-output-location/vendor/external.cpp"},
                    ],
                },
            )
            inventory = CMakeInventory.read(directory)
            self.assertEqual(inventory.targets[0].sources, ("src/core.cpp",))

    def test_explicit_owned_sources_take_precedence_over_a_build_boundary(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            source, _ = self.model(directory)
            mixed = source / "module-output"
            mixed.mkdir(parents=True)
            (mixed / "CMakeCache.txt").write_text("# configured module boundary")
            self.write(
                directory,
                "core.json",
                {
                    "type": "STATIC_LIBRARY",
                    "sources": [
                        {"path": "module-output/owned.cpp"},
                        {"path": "module-output/vendor/external.cpp"},
                    ],
                },
            )
            inventory = CMakeInventory.read(
                directory, first_party_sources={"module-output/owned.cpp"}
            )
            self.assertEqual(inventory.targets[0].sources, ("module-output/owned.cpp",))


if __name__ == "__main__":
    unittest.main()
