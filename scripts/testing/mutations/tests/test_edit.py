from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from scripts.testing.mutations._edit import SourceConflict, SourceMutation


class SourceMutationTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.path = self.root / "source with spaces.cpp"
        self.original = b"\xff\r\nif (valid) {\r\nreturn;\r\n}\r\n"
        self.path.write_bytes(self.original)
        self.path.chmod(0o640)

    def mutation(self):
        return SourceMutation(self.path, b"if (valid)", b"if (false)")

    def test_exact_bytes_and_permissions_are_restored_on_success_and_exceptions(self):
        for error in (
            None,
            RuntimeError("build failed"),
            KeyboardInterrupt("interrupted"),
        ):
            mutation = self.mutation()
            try:
                with mutation:
                    self.assertIn(b"if (false)", self.path.read_bytes())
                    self.assertFalse(mutation.restored)
                    if error is not None:
                        raise error
            except BaseException as caught:
                self.assertIs(caught, error)
            self.assertTrue(mutation.restored)
            self.assertEqual(self.path.read_bytes(), self.original)
            self.assertEqual(self.path.stat().st_mode & 0o777, 0o640)
            self.assertNotEqual(mutation.original_sha256, mutation.mutant_sha256)
            self.assertFalse(list(self.root.glob(".wiola-mutation-*")))

    def test_bad_or_duplicate_anchors_cannot_change_source(self):
        for anchor, replacement in (
            (b"", b"new"),
            (b"missing", b"new"),
            (b"return", b"return"),
        ):
            with self.assertRaises(ValueError):
                SourceMutation(self.path, anchor, replacement)
        self.path.write_bytes(b"anchor anchor")
        with self.assertRaises(ValueError):
            SourceMutation(self.path, b"anchor", b"replacement")
        self.assertEqual(self.path.read_bytes(), b"anchor anchor")

    def test_edits_before_or_during_mutation_are_never_overwritten(self):
        mutation = self.mutation()
        self.path.write_bytes(b"external edit")
        with self.assertRaises(SourceConflict):
            with mutation:
                self.fail("A changed source must not enter the mutation scope")
        self.assertEqual(self.path.read_bytes(), b"external edit")
        self.path.write_bytes(self.original)
        with self.assertRaises(SourceConflict):
            with self.mutation():
                self.path.write_bytes(b"concurrent edit")
        self.assertEqual(self.path.read_bytes(), b"concurrent edit")
        self.path.write_bytes(self.original)
        with self.assertRaises(SourceConflict):
            with self.mutation():
                self.path.unlink()
        self.assertFalse(self.path.exists())

    def test_failed_replacement_leaves_original_bytes_and_no_temporary_files(self):
        mutation = self.mutation()
        with patch(
            "scripts.testing.mutations._edit.os.replace",
            side_effect=OSError("disk failure"),
        ):
            with self.assertRaises(OSError):
                with mutation:
                    self.fail("A failed replacement must not enter the mutation scope")
        self.assertTrue(mutation.restored)
        self.assertFalse(list(self.root.glob(".wiola-mutation-*")))

    def test_unwritable_source_directory_fails_before_any_source_edit(self):
        mutation = self.mutation()
        with patch(
            "scripts.testing.mutations._edit.tempfile.NamedTemporaryFile",
            side_effect=PermissionError("read-only directory"),
        ):
            with self.assertRaises(PermissionError):
                with mutation:
                    self.fail(
                        "A failed temporary write must not enter the mutation scope"
                    )
        self.assertTrue(mutation.restored)
