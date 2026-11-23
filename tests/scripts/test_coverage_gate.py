import copy
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

SCRIPTS = Path(__file__).resolve().parents[2] / 'scripts'
sys.path.insert(0, str(SCRIPTS))
import coverage_gate as gate
spec = importlib.util.spec_from_file_location('mutations', SCRIPTS / 'check-gui-mutations.py')
mutations = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mutations)


def report(total=100, hit=90):
    counts = {m: {'count': total, 'covered': hit} for m in gate.METRICS}
    return {'totals': copy.deepcopy(counts),
            'files': [{'file': 'src/gui/view.cpp', 'counts': counts}]}


class CoverageGate(unittest.TestCase):
    def test_exact_full_coverage_is_not_rounded_up(self):
        self.assertTrue(gate.compare(report(10000, 9999), report(10000, 10000)))
        self.assertEqual(gate.compare(report(10000, 10000), report(10000, 10000)), [])

    def test_removed_mapping_and_smaller_denominator_are_rejected(self):
        missing = report(); missing['files'] = []
        self.assertTrue(gate.compare(missing, report()))
        self.assertTrue(gate.compare(report(99, 99), report()))

    def test_new_uncovered_file_cannot_hide_behind_an_improved_total(self):
        current = report(200, 200)
        extra = report(5, 0)['files'][0]; extra['file'] = 'src/gui/new.cpp'
        current['files'].append(extra)
        self.assertTrue(any('no covered' in f for f in gate.compare(current, report())))

    def test_counts_must_be_valid_and_source_inventory_unique(self):
        for total, hit in [(-1, 0), (1, 2), (1, -1), (1, True)]:
            with self.assertRaises(ValueError): gate.compare(report(total, hit), report())
        duplicate = report(); duplicate['files'] *= 2
        with self.assertRaises(ValueError): gate.compare(duplicate, report())

    def test_mapped_changed_line_and_each_branch_must_have_hits(self):
        diff = '+++ b/src/gui/view.cpp\n@@ -8,0 +9,3 @@\n+if (valid) {\n+    apply();\n+}\n'
        changes = gate.changed_lines(diff)
        self.assertEqual(changes, {'src/gui/view.cpp': {9, 10, 11}})
        lcov = 'SF:/tmp/repo/src/gui/view.cpp\nDA:9,1\nDA:10,0\nBRDA:9,0,0,1\nBRDA:9,0,1,-\nend_of_record\n'
        failures, unmapped = gate.check_changed(changes, gate.lcov_records(lcov, Path('/tmp/repo')))
        self.assertEqual(len(failures), 2)
        self.assertEqual(unmapped, ['src/gui/view.cpp:11'])
        fixed = lcov.replace('DA:10,0', 'DA:10,1').replace('BRDA:9,0,1,-', 'BRDA:9,0,1,1')
        self.assertEqual(gate.check_changed(changes, gate.lcov_records(fixed, Path('/tmp/repo')))[0], [])

    def test_missing_tests_profiles_or_changed_exclusions_cannot_pass(self):
        baseline = {'test_names': ['Critical.Test'], 'exclusions': ['external dependencies']}
        manifest = {'completed': {'target': {'tests': ['Critical.Test'], 'profiles': 1}},
                    'raw_profiles': 1, 'exclusions': baseline['exclusions']}
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory); raw = report / 'raw/target'; raw.mkdir(parents=True)
            with self.assertRaises(ValueError): gate.check_manifest(manifest, baseline, report)
            (raw / 'process.profraw').write_bytes(b'actual process profile')
            self.assertEqual(gate.check_manifest(manifest, baseline, report), [])
            manifest['completed']['target']['tests'] = []
            self.assertTrue(gate.check_manifest(manifest, baseline, report))
            manifest['exclusions'] = ['all difficult code']
            self.assertTrue(any('exclusions' in f for f in gate.check_manifest(manifest, baseline, report)))
            manifest['raw_profiles'] = 2
            self.assertTrue(any('total disagrees' in f for f in gate.check_manifest(manifest, baseline, report)))

    def test_deletions_do_not_shift_added_line_numbers(self):
        diff = '+++ b/src/a.cpp\n@@ -1,2 +1,2 @@\n-old\n+new\n same\n@@ -8,0 +9 @@\n+next\n'
        self.assertEqual(gate.changed_lines(diff), {'src/a.cpp': {1, 9}})


class MutationEvidence(unittest.TestCase):
    def test_only_an_executed_assertion_failure_kills_a_mutant(self):
        with tempfile.TemporaryDirectory() as directory:
            xml = Path(directory) / 'results.xml'
            self.assertFalse(mutations.assertion_failure(xml, 1))
            for tests, failures, errors, status, killed in [
                (1, 1, 0, 1, True), (0, 0, 0, 0, False), (1, 0, 0, 0, False),
                (1, 1, 0, -11, False), (1, 0, 1, 1, False)]:
                xml.write_text(f'<testsuites tests="{tests}" failures="{failures}" errors="{errors}"/>')
                self.assertEqual(mutations.assertion_failure(xml, status), killed)


if __name__ == '__main__': unittest.main()
