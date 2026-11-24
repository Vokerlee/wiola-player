from pathlib import Path
import sys
import tempfile
import unittest

from scripts.quality.coverage.llvm_reporting.reporter import (
    LLVMReporter,
    LLVMReportError,
    MappingDiagnostics,
)

DETAIL = "warning: 1 functions have mismatched data\nhash-mismatch: No profile record found for 'unused' with hash = 0x0\n"
SUMMARY = "warning: 1 functions have mismatched data\n"


class MappingDiagnosticsTest(unittest.TestCase):
    def test_detailed_and_summary_counts_must_agree_without_hiding_other_warnings(self):
        diagnostics = MappingDiagnostics.read("\x1b[31m" + DETAIL + "\x1b[0m\n")
        self.assertEqual(diagnostics.zero_hash_mappings, ("unused",))
        diagnostics.verify_summary(SUMMARY)
        self.assertEqual(MappingDiagnostics.read("").zero_hash_mappings, ())
        for source in (
            DETAIL.replace("0x0", "0x12"),
            SUMMARY,
            "warning: unknown input\n",
        ):
            with self.assertRaises(ValueError):
                MappingDiagnostics.read(source)
        for source in ("", "warning: unknown input\n"):
            with self.assertRaises(ValueError):
                diagnostics.verify_summary(source)


class LLVMReporterTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.tool = self.root / "controlled_llvm.py"
        self.tool.write_text(
            "import sys\n"
            f"detailed = {DETAIL!r}\nsummary = {SUMMARY!r}\n"
            "if 'abort' in sys.argv:\n"
            "    print('LLVM ERROR: controlled backend failure', file=sys.stderr)\n"
            "    raise SystemExit(6)\n"
            "if 'unexpected' in sys.argv:\n"
            "    print('warning: unknown input', file=sys.stderr)\n"
            "else:\n"
            "    sys.stderr.write(detailed if 'report' in sys.argv else summary)\n"
            "if 'report' in sys.argv:\n"
            "    assert '-dump' in sys.argv\n"
            "    assert '-num-threads=1' in sys.argv\n"
            "else:\n"
            "    assert '-dump' not in sys.argv\n"
            "    assert '-num-threads=1' not in sys.argv\n"
        )
        self.reporter = LLVMReporter(
            (sys.executable, str(self.tool)), self.root / "evidence"
        )

    def test_success_requires_detailed_phase_evidence_and_returns_an_independent_snapshot(
        self,
    ):
        self.reporter.execute(["report", "object path"])
        self.reporter.execute(["export", "object path"])
        self.reporter.execute(["show", "object path"])
        snapshot = self.reporter.diagnostics
        self.assertEqual(snapshot, {"whole": {"zero_hash_mappings": ["unused"]}})
        snapshot["whole"]["zero_hash_mappings"].clear()
        self.assertEqual(
            self.reporter.diagnostics["whole"]["zero_hash_mappings"], ["unused"]
        )
        self.assertEqual(
            (self.root / "evidence/whole-report-mapping-diagnostics.txt").read_text(),
            DETAIL,
        )
        for arguments, phase in (
            ([], "whole"),
            (["other"], "whole"),
            (["report"], "../outside"),
            (["export"], "unobserved"),
        ):
            with self.assertRaises(ValueError):
                self.reporter.execute(arguments, phase=phase)

    def test_failed_and_invalid_successful_commands_keep_raw_stderr_before_failing(
        self,
    ):
        self.reporter.execute(["report", "object path"])
        with self.assertRaisesRegex(LLVMReportError, "controlled backend failure"):
            self.reporter.execute(["report", "abort"])
        diagnostic = self.root / "evidence/whole-report-mapping-diagnostics.txt"
        self.assertIn("LLVM ERROR", diagnostic.read_text())
        self.assertEqual(self.reporter.diagnostics, {})
        with self.assertRaises(ValueError):
            self.reporter.execute(["export", "object path"])
        with self.assertRaises(ValueError):
            self.reporter.execute(["report", "unexpected"])
        self.assertEqual(diagnostic.read_text(), "warning: unknown input\n")
