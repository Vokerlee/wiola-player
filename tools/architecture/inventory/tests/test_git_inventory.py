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


if __name__ == "__main__":
    unittest.main()
