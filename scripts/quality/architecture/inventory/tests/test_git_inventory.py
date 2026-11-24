from pathlib import Path
import subprocess
import tempfile
import unittest

from scripts.quality.architecture.inventory.git_inventory import GitInventory


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
            self.assertEqual(adapter.root, root.resolve())
            reference = adapter.revision("HEAD")
            file.write_text("working bytes")
            self.assertEqual(adapter.read("source.txt", reference), "trusted bytes")
            self.assertEqual(adapter.read("source.txt"), "working bytes")
            self.assertIn("tests: preserve", adapter.commit_message())
            self.assertEqual(adapter.deleted_paths(reference), ())
            for invalid in ["../source.txt", "/source.txt", "nested/../source.txt"]:
                with self.assertRaises(ValueError):
                    adapter.read(invalid)

    def test_previous_policy_follows_committed_renames_and_keeps_trusted_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            def git(*arguments):
                return subprocess.check_output(
                    ["git", *arguments], cwd=root, text=True
                ).strip()

            def commit():
                git("add", "-A")
                git(
                    "-c",
                    "user.name=Fixture",
                    "-c",
                    "user.email=fixture@example.invalid",
                    "commit",
                    "--quiet",
                    "-m",
                    "tests: advance trusted fixture",
                )

            git("init", "--quiet")
            old = root / "policy 音楽.json"
            trusted = "\n".join(f"trusted decision {i}" for i in range(30))
            old.write_text(trusted)
            commit()
            before = git("rev-parse", "HEAD")
            current = root / "nested" / "renamed policy.json"
            current.parent.mkdir()
            old.rename(current)
            current.write_text(trusted + "\nnew weaker decision")
            commit()
            adapter = GitInventory(current.parent)
            self.assertEqual(
                adapter.read_previous("nested/renamed policy.json", before), trusted
            )
            self.assertEqual(
                adapter.read_previous("nested/renamed policy.json", "HEAD"),
                current.read_text(),
            )
            current.write_text("uncommitted current bytes must never become trusted")
            index = git("ls-files", "--stage")
            self.assertEqual(
                adapter.read_previous("nested/renamed policy.json", before), trusted
            )
            self.assertEqual(git("ls-files", "--stage"), index)
            for invalid in ("../policy.json", "/policy.json", "nested/../policy.json"):
                with self.assertRaises(ValueError):
                    adapter.read_previous(invalid, before)
            with self.assertRaises(subprocess.CalledProcessError):
                adapter.read_previous("nested/renamed policy.json", "missing-reference")

    def test_new_or_unmatched_policy_cannot_borrow_unrelated_trusted_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            def git(*arguments):
                return subprocess.check_output(
                    ["git", *arguments], cwd=root, text=True
                ).strip()

            def commit():
                git("add", "-A")
                git(
                    "-c",
                    "user.name=Fixture",
                    "-c",
                    "user.email=fixture@example.invalid",
                    "commit",
                    "--quiet",
                    "-m",
                    "tests: advance trusted fixture",
                )

            git("init", "--quiet")
            (root / "old.json").write_text("unrelated trusted policy")
            commit()
            before = git("rev-parse", "HEAD")
            (root / "old.json").rename(root / "renamed.json")
            (root / "new.json").write_text("a genuinely new policy")
            commit()
            adapter = GitInventory(root)
            for name in ("new.json", "missing.json"):
                with self.assertRaises(subprocess.CalledProcessError):
                    adapter.read_previous(name, before)

    def test_worktree_reads_reject_a_symlink_outside_the_owning_repository(self):
        with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryDirectory() as external:
            root = Path(directory)
            subprocess.run(["git", "init", "--quiet", str(root)], check=True)
            target = Path(external) / "private-policy.json"
            target.write_text("external contents must not be read")
            (root / "linked.json").symlink_to(target)
            with self.assertRaisesRegex(ValueError, "escapes"):
                GitInventory(root).read("linked.json")


if __name__ == "__main__":
    unittest.main()
