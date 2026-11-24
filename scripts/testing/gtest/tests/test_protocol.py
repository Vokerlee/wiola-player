import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

from scripts.testing.gtest import GoogleTestReport, discovered_cases
from scripts.testing.gtest import (
    GoogleTestResult,
    GoogleTestRunner,
    TestExecutionError,
)

PASS_REPORT = """<testsuites tests="2" failures="0" errors="0"><testsuite name="Suite">
<testcase classname="Suite" name="one" status="run" result="completed"/>
<testcase classname="Suite" name="two" status="run" result="completed"/>
</testsuite></testsuites>"""
FAIL_REPORT = PASS_REPORT.replace('failures="0"', 'failures="1"').replace(
    'name="one" status="run" result="completed"/>',
    'name="one" status="run" result="completed"><failure message="expected contract"/></testcase>',
)


class GoogleTestProtocolTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.xml = self.root / "input.xml"

    def read(self, source):
        self.xml.write_text(source)
        return GoogleTestReport.read(self.xml)

    def test_parameterized_discovery_uses_case_identities_and_rejects_empty_or_duplicates(
        self,
    ):
        cases = discovered_cases(
            "Running main() from gtest.cc\nKinds/Suite. # TypeParam = int\n  works/0 # GetParam() = 7\n"
        )
        self.assertEqual(cases, {"Kinds/Suite.works/0"})
        for text in ("", "Suite.\n  one\n  one\n", "  case_without_suite\n"):
            with self.assertRaises(ValueError):
                discovered_cases(text)

    def test_xml_needs_exact_completed_cases_and_consistent_counters(self):
        report = self.read(PASS_REPORT)
        self.assertTrue(report.covers(frozenset({"Suite.one", "Suite.two"})))
        self.assertFalse(report.covers(frozenset({"Suite.one"})))
        for source in (
            PASS_REPORT.replace('tests="2"', 'tests="3"'),
            PASS_REPORT.replace('name="two"', 'name="one"'),
            PASS_REPORT.replace('classname="Suite"', 'classname=""'),
            '<testsuites tests="0" failures="0" errors="0"/>',
            "<not-google-test/>",
        ):
            with self.assertRaises(ValueError):
                self.read(source)
        skipped = self.read(PASS_REPORT.replace('status="run"', 'status="notrun"', 1))
        self.assertFalse(skipped.covers(frozenset({"Suite.one", "Suite.two"})))
        skipped_node = self.read(
            PASS_REPORT.replace(
                'result="completed"/>', 'result="completed"><skipped/></testcase>', 1
            )
        )
        self.assertFalse(skipped_node.covers(frozenset({"Suite.one", "Suite.two"})))

    def test_only_an_executed_assertion_failure_kills_a_mutant(self):
        selected = frozenset({"Suite.one", "Suite.two"})
        failed = self.read(FAIL_REPORT)
        self.assertTrue(
            GoogleTestResult(1, selected, failed, self.root).assertion_failure
        )
        for code in (0, -11, 134, 3221225477):
            result = GoogleTestResult(code, selected, failed, self.root)
            self.assertFalse(result.assertion_failure)
            self.assertFalse(result.passed)
        errored = self.read(
            FAIL_REPORT.replace('errors="0"', 'errors="1"').replace(
                '<failure message="expected contract"/>',
                '<failure message="expected contract"/><error/>',
            )
        )
        self.assertFalse(
            GoogleTestResult(1, selected, errored, self.root).assertion_failure
        )


@unittest.skipIf(os.name == "nt", "Controlled executable fixtures use Unix shebangs")
class GoogleTestProcessTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.binary = self.root / "test executable"
        self.artifacts = self.root / "evidence with spaces"
        self.binary.write_text(
            f"#!{sys.executable}\n"
            "import os, pathlib, sys, time\n"
            "mode = os.environ.get('PROTOCOL_FIXTURE_MODE', 'pass')\n"
            "if '--gtest_list_tests' in sys.argv:\n"
            "    if mode == 'bad_discovery': sys.exit(2)\n"
            "    if mode == 'empty_discovery': sys.exit(0)\n"
            "    print('Suite.\\n  one\\n  two')\n"
            "    sys.exit(0)\n"
            "if mode == 'timeout': time.sleep(5)\n"
            "if mode == 'no_report': sys.exit(0)\n"
            f"report = {PASS_REPORT!r}\n"
            f"if mode in ('assertion', 'crash'): report = {FAIL_REPORT!r}\n"
            "if mode == 'incomplete': report = report.replace('status=\"run\"', 'status=\"notrun\"', 1)\n"
            "if mode == 'malformed': report = 'not xml'\n"
            "path = next(a.split('xml:', 1)[1] for a in sys.argv if a.startswith('--gtest_output=xml:'))\n"
            "pathlib.Path(path).write_text(report)\n"
            "if mode == 'replace_binary':\n"
            "    with pathlib.Path(__file__).open('a') as source: source.write('\\n# changed\\n')\n"
            "print('contract process output')\n"
            "sys.exit(1 if mode == 'assertion' else (3 if mode == 'crash' else 0))\n"
        )
        self.binary.chmod(0o755)

    def run_contract(self, mode="pass", **changes):
        arguments = dict(
            environment=dict(os.environ, PROTOCOL_FIXTURE_MODE=mode),
            revision="test-revision",
            timeout=10,
        )
        arguments.update(changes)
        return GoogleTestRunner().run(
            self.binary, "Suite.*", self.artifacts, **arguments
        )

    def test_success_records_selected_cases_hash_and_separate_process_logs(self):
        original = dict(os.environ)
        self.assertTrue(self.run_contract().passed)
        manifest = json.loads((self.artifacts / "manifest.json").read_text())
        self.assertEqual(manifest["selected"], ["Suite.one", "Suite.two"])
        self.assertEqual(manifest["revision"], "test-revision")
        self.assertEqual(len(manifest["sha256"]), 64)
        self.assertNotIn("environment", manifest)
        self.assertTrue((self.artifacts / "discovery.log").is_file())
        self.assertIn(
            "contract process output", (self.artifacts / "execution.log").read_text()
        )
        self.assertEqual(dict(os.environ), original)

    def test_missing_or_invalid_report_discovery_failure_and_changed_binary_fail(self):
        for mode in (
            "no_report",
            "malformed",
            "bad_discovery",
            "empty_discovery",
            "replace_binary",
        ):
            with self.subTest(mode=mode), self.assertRaises(TestExecutionError):
                self.run_contract(mode)

    def test_stale_xml_cannot_turn_a_timeout_into_success(self):
        self.artifacts.mkdir()
        xml = self.artifacts / "results.xml"
        xml.write_text(PASS_REPORT)
        with self.assertRaises(TestExecutionError):
            self.run_contract("timeout", timeout=0.1)
        self.assertFalse(xml.exists())

    def test_assertion_evidence_is_accepted_but_crashes_and_incomplete_cases_are_not(
        self,
    ):
        self.assertTrue(self.run_contract("assertion").assertion_failure)
        for mode in ("crash", "incomplete"):
            result = self.run_contract(mode)
            self.assertFalse(result.passed)
            self.assertFalse(result.assertion_failure)

    def test_bounds_missing_binaries_and_omitted_required_cases_are_rejected(self):
        for changes in ({"timeout": 0}, {"minimum_tests": 0}):
            with self.assertRaises(ValueError):
                self.run_contract(**changes)
        for changes in (
            {"minimum_tests": 3},
            {"required_cases": frozenset({"Suite.absent"})},
        ):
            with self.assertRaises(TestExecutionError):
                self.run_contract(**changes)
        self.binary.unlink()
        with self.assertRaises(TestExecutionError):
            self.run_contract()
