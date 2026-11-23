import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('coverage_report', Path(__file__).resolve().parents[2] / 'scripts/coverage_report.py')
coverage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(coverage)


class CoverageInventory(unittest.TestCase):
    def test_missing_or_empty_profile_fails_instead_of_reusing_an_old_report(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            with self.assertRaises(ValueError): coverage.require_profiles(path, 1)
            (path / 'empty.profraw').touch()
            with self.assertRaises(ValueError): coverage.require_profiles(path, 1)
            (path / 'empty.profraw').write_bytes(b'a nonempty process profile')
            self.assertEqual(len(coverage.require_profiles(path, 1)), 1)
            with self.assertRaises(ValueError): coverage.require_profiles(path, 2)

    def test_only_detailed_zero_hash_mapping_sentinels_are_accepted(self):
        sentinel = "warning: 1 functions have mismatched data\nhash-mismatch: No profile record found for 'unused_inline' with hash = 0x0\n"
        self.assertEqual(coverage.coverage_diagnostics(sentinel), ['unused_inline'])
        with self.assertRaises(ValueError): coverage.coverage_diagnostics(sentinel.replace('0x0', '0x1234'))
        with self.assertRaises(ValueError): coverage.coverage_diagnostics(sentinel.splitlines()[0])
        with self.assertRaises(ValueError): coverage.coverage_diagnostics('warning: ignored profile')

    def test_extracted_workflow_files_remain_in_scope(self):
        root = Path('/tmp/repo')
        for folder in ['gui', 'desktop', 'app', 'persistence', 'library', 'media']:
            self.assertTrue(coverage.workflow_file(root / 'src' / folder / 'moved.cpp', root))
        self.assertFalse(coverage.workflow_file(root / 'tests/gui/test.cpp', root))

    def test_zero_hits_and_distinct_branch_counts_remain_visible(self):
        summary = {metric: {'count': count, 'covered': 0} for metric, count in
                   [('lines', 7), ('functions', 2), ('regions', 9), ('branches', 4)]}
        totals = coverage.summarize([{'summary': summary}])
        self.assertEqual(totals['lines']['uncovered'], 7)
        self.assertEqual(totals['branches']['uncovered'], 4)


if __name__ == '__main__': unittest.main()
