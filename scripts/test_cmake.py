"""Artifact resolution must follow configured metadata without path conventions."""

import json
from pathlib import Path
import tempfile
import unittest

from scripts.cmake import target_artifact


class CMakeArtifactTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="cmake artifacts ")
        self.addCleanup(temporary.cleanup)
        self.build = Path(temporary.name)
        self.reply = self.build / ".cmake/api/v1/reply"
        self.reply.mkdir(parents=True)
        self.write("index-fixture.json", {
            "reply": {"codemodel-v2": {"jsonFile": "model.json"}}
        })
        self.model = {
            "version": {"major": 2},
            "configurations": [{"targets": [{"name": "client", "jsonFile": "target.json"}]}],
        }
        self.write("model.json", self.model)
        self.target = {
            "type": "EXECUTABLE",
            "artifacts": [{"path": "unusual output/renamed program"}],
        }
        self.write("target.json", self.target)

    def write(self, name, value):
        (self.reply / name).write_text(json.dumps(value))

    def test_uses_declared_relative_or_absolute_path_and_primary_artifact(self):
        expected = self.build / "unusual output/renamed program"
        self.assertEqual(target_artifact(self.build, "client", "EXECUTABLE"), expected)

        self.target["artifacts"] = [{"path": str(expected)}, {"path": "debug.pdb"}]
        self.target["nameOnDisk"] = expected.name
        self.write("target.json", self.target)
        self.assertEqual(target_artifact(self.build, "client", "EXECUTABLE"), expected)

    def test_rejects_unknown_ambiguous_or_wrong_kind_targets(self):
        for name, kind in (("missing", "EXECUTABLE"), ("client", "SHARED_LIBRARY")):
            with self.assertRaises(ValueError):
                target_artifact(self.build, name, kind)

        self.target["artifacts"].append({"path": "other program"})
        self.write("target.json", self.target)
        with self.assertRaises(ValueError):
            target_artifact(self.build, "client", "EXECUTABLE")

    def test_rejects_missing_corrupt_or_unsupported_codemodel(self):
        for value in ({}, {**self.model, "configurations": []},
                      {**self.model, "version": {"major": 3}}):
            self.write("model.json", value)
            with self.assertRaises(ValueError):
                target_artifact(self.build, "client", "EXECUTABLE")

        (self.reply / "model.json").write_text("not json")
        with self.assertRaises(ValueError):
            target_artifact(self.build, "client", "EXECUTABLE")

        (self.reply / "index-fixture.json").unlink()
        with self.assertRaises(ValueError):
            target_artifact(self.build, "client", "EXECUTABLE")

    def test_rejects_reply_references_outside_the_reply_directory(self):
        self.model["configurations"][0]["targets"][0]["jsonFile"] = "../target.json"
        self.write("model.json", self.model)
        with self.assertRaises(ValueError):
            target_artifact(self.build, "client", "EXECUTABLE")
