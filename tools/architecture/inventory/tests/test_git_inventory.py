from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.architecture.inventory.git_inventory import GitInventory


class GitInventoryTest(unittest.TestCase):
    def test_reads_only_tracked_paths_and_preserves_spaces_and_unicode(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "--quiet", str(root)], check=True)
            tracked = root / "音楽 file.cpp"
            tracked.write_text("// fixture\n")
            (root / "local-plan.md").write_text("untracked working plan\n")
            subprocess.run(["git", "add", tracked.name], cwd=root, check=True)
            self.assertEqual(GitInventory(root).paths(), (tracked.name,))
            nested = root / "nested"
            nested.mkdir()
            self.assertEqual(GitInventory(nested).paths(), (tracked.name,))
            tracked.unlink()
            # An unstaged deletion still belongs to the index until explicitly removed.
            self.assertEqual(GitInventory(root).paths(), (tracked.name,))
            subprocess.run(["git", "rm", "--quiet", tracked.name], cwd=root, check=True)
            self.assertEqual(GitInventory(root).paths(), ())

    def test_invalid_repository_preserves_subprocess_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(subprocess.CalledProcessError):
                GitInventory(Path(directory)).paths()

    def test_historical_reads_keep_repository_identity_and_do_not_change_the_index(
        self,
    ):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "--quiet", str(root)], check=True)
            file = root / "source.txt"
            file.write_text("trusted bytes")
            subprocess.run(["git", "add", file.name], cwd=root, check=True)
            subprocess.run(
                [
                    "git",
                    "-c",
                    "user.name=Fixture",
                    "-c",
                    "user.email=fixture@example.invalid",
                    "commit",
                    "--quiet",
                    "-m",
                    "tests: preserve a trusted fixture",
                ],
                cwd=root,
                check=True,
            )
            nested = root / "nested"
            nested.mkdir()
            adapter = GitInventory(nested)
            reference = adapter.revision("HEAD")
            file.write_text("working bytes")
            self.assertEqual(adapter.read("source.txt", reference), "trusted bytes")
            self.assertEqual(adapter.read("source.txt"), "working bytes")
            self.assertIn("tests: preserve", adapter.commit_message())
            self.assertEqual(adapter.deleted_paths(reference), ())
            for invalid in ["../source.txt", "/source.txt", "nested/../source.txt"]:
                with self.assertRaises(ValueError):
                    adapter.read(invalid)


if __name__ == "__main__":
    unittest.main()
