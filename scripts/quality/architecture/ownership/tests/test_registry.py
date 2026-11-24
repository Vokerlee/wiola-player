import json
import unittest

from scripts.quality.architecture.ownership.registry import (
    OwnershipRegistry,
    RegistryError,
)


def declaration(identity="engine.session", paths=None, purpose="Coordinates playback"):
    return {
        "id": identity,
        "purpose": purpose,
        "files": ["src/engine/session.hpp"] if paths is None else paths,
    }


def document(*owners, version=1):
    return json.dumps({"schema_version": version, "owners": list(owners)})


class OwnershipRegistryTest(unittest.TestCase):
    def test_fragments_resolve_each_file_to_one_stable_owner(self):
        registry = OwnershipRegistry(
            [
                document(
                    declaration(
                        paths=["src/engine/session.hpp", "src/engine/session.cpp"]
                    )
                ),
                document(
                    declaration(
                        "media.provider",
                        ["src/core/media/public/wiola/media/provider.hpp"],
                        "Identifies media",
                    )
                ),
            ]
        )
        self.assertEqual(
            registry.owner_of("src/engine/session.cpp").identity, "engine.session"
        )
        self.assertTrue(
            registry.compare(
                [
                    "src/core/media/public/wiola/media/provider.hpp",
                    "src/engine/session.hpp",
                    "src/engine/session.cpp",
                ]
            ).matches
        )
        self.assertEqual(
            [owner.identity for owner in registry.owners],
            ["engine.session", "media.provider"],
        )

    def test_comparison_distinguishes_unowned_and_stale_files(self):
        registry = OwnershipRegistry([document(declaration())])
        difference = registry.compare(["src/new.cpp"])
        self.assertFalse(difference.matches)
        self.assertEqual(difference.unowned, ("src/new.cpp",))
        self.assertEqual(difference.stale, ("src/engine/session.hpp",))
        with self.assertRaises(KeyError):
            registry.owner_of("src/new.cpp")

    def test_duplicate_identity_or_file_is_not_last_writer_wins(self):
        cases = [
            [declaration(), declaration(paths=["src/other.hpp"])],
            [declaration(), declaration("other.owner")],
            [declaration(paths=["src/same.hpp", "src/same.hpp"])],
        ]
        for owners in cases:
            with self.subTest(owners=owners), self.assertRaises(RegistryError):
                OwnershipRegistry([document(*owners)])
        with self.assertRaises(RegistryError):
            OwnershipRegistry([document(declaration()), document(declaration())])

    def test_paths_are_canonical_portable_repository_paths(self):
        for path in [
            "",
            ".",
            "/absolute",
            "../outside",
            "src/../outside",
            "src/./x",
            "src//x",
            "src/",
            "C:/absolute",
            "src\\x",
            ".git/config",
            "bad\nname",
        ]:
            with self.subTest(path=path), self.assertRaises(RegistryError):
                OwnershipRegistry([document(declaration(paths=[path]))])
        registry = OwnershipRegistry(
            [document(declaration(paths=["src/音楽 player.hpp"]))]
        )
        self.assertEqual(
            registry.owner_of("src/音楽 player.hpp").identity, "engine.session"
        )

    def test_schema_and_owner_shapes_fail_explicitly(self):
        malformed = [
            "{",
            "[]",
            "{}",
            '{"schema_version":1,"owners":{}}',
            '{"schema_version":1,"owners":[],"unknown":true}',
        ]
        malformed += [
            document(declaration(), version=value) for value in [True, 0, 2, "1"]
        ]
        malformed += [document(), document({"id": "missing.fields"})]
        for value in malformed:
            with self.subTest(value=value), self.assertRaises(RegistryError):
                OwnershipRegistry([value])
        with self.assertRaises(RegistryError):
            OwnershipRegistry([])

    def test_invalid_owner_contracts_are_rejected(self):
        invalid = [
            declaration(identity=value) for value in ["", "Upper", "with-dash", 3]
        ]
        invalid += [declaration(purpose=value) for value in ["", " ", " padded", None]]
        invalid += [declaration(paths=value) for value in [[], "src/file", [None]]]
        invalid += [dict(declaration(), future_field=True)]
        for value in invalid:
            with self.subTest(value=value), self.assertRaises(RegistryError):
                OwnershipRegistry([document(value)])

    def test_duplicate_json_keys_are_rejected(self):
        value = '{"schema_version":1,"schema_version":2,"owners":[]}'
        with self.assertRaisesRegex(RegistryError, "Duplicate JSON key"):
            OwnershipRegistry([value])

    def test_returned_owner_files_cannot_be_mutated(self):
        registry = OwnershipRegistry([document(declaration())])
        with self.assertRaises(AttributeError):
            registry.owners[0].files.add("src/injected.hpp")


if __name__ == "__main__":
    unittest.main()
