from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path
import tempfile
from types import SimpleNamespace
from unittest.mock import patch
import os
import unittest

from scripts.testing.desktop.environment import DesktopEnvironment
from scripts.testing.desktop.__main__ import main


class DesktopEnvironmentTest(unittest.TestCase):
    def test_explicit_fixture_paths_preserve_inherited_gui_and_copy_the_mapping(self):
        with tempfile.TemporaryDirectory() as temporary:
            media = Path(temporary) / "media fixtures"
            media.mkdir()
            original = {
                "QT_QPA_PLATFORM": "custom-platform",
                "OTHER_GUI_SETTING": "retained",
            }
            result = DesktopEnvironment(Path(temporary) / "evidence", media).apply(
                original
            )
            self.assertEqual(result["QT_QPA_PLATFORM"], "custom-platform")
            self.assertEqual(result["WIOLA_JOURNEY_MEDIA_DIR"], str(media.resolve()))
            self.assertEqual(
                original,
                {"QT_QPA_PLATFORM": "custom-platform", "OTHER_GUI_SETTING": "retained"},
            )
            explicit = DesktopEnvironment(
                Path(temporary), qt_platform="offscreen"
            ).apply(original)
            self.assertEqual(explicit["QT_QPA_PLATFORM"], "offscreen")
            without_qt = DesktopEnvironment(Path(temporary)).apply({})
            self.assertNotIn("QT_QPA_PLATFORM", without_qt)

    def test_missing_media_and_invalid_cli_inputs_fail_with_context(self):
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaises(ValueError):
                DesktopEnvironment(Path(temporary), Path(temporary) / "missing").apply(
                    {}
                )
            output = StringIO()
            with redirect_stderr(output):
                code = main(
                    [
                        "--binary",
                        str(Path(temporary) / "missing"),
                        "--artifacts",
                        temporary,
                        "--revision",
                        "local",
                        "--timeout",
                        "0",
                    ]
                )
            self.assertEqual(code, 1)
            self.assertIn("Desktop check failed", output.getvalue())

    def test_cli_preserves_explicit_contracts_and_reports_success_or_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            media = root / "input media"
            media.mkdir()
            evidence = root / "execution evidence"
            evidence.mkdir()
            (evidence / "execution.log").write_text("selected contract output")
            inherited = dict(os.environ)
            for passed in (True, False):
                with self.subTest(passed=passed):
                    out, err = StringIO(), StringIO()
                    with patch(
                        "scripts.testing.desktop.__main__.GoogleTestRunner"
                    ) as runner, redirect_stdout(out), redirect_stderr(err):
                        runner.return_value.run.return_value = SimpleNamespace(
                            artifacts=evidence, passed=passed
                        )
                        code = main(
                            [
                                "--binary",
                                str(root / "caller executable"),
                                "--artifacts",
                                str(evidence),
                                "--revision",
                                "reviewed-revision",
                                "--filter",
                                "ChosenSuite.*",
                                "--media-dir",
                                str(media),
                                "--qt-platform",
                                "caller-platform",
                                "--timeout",
                                "7",
                                "--minimum-tests",
                                "2",
                                "--require-case",
                                "ChosenSuite.first",
                                "--require-case",
                                "ChosenSuite.second",
                            ]
                        )
                    self.assertEqual(code, 0 if passed else 1)
                    self.assertIn("selected contract output", out.getvalue())
                    self.assertEqual(bool(err.getvalue()), not passed)
                    args, kwargs = runner.return_value.run.call_args
                    self.assertEqual(
                        args, (root / "caller executable", "ChosenSuite.*", evidence)
                    )
                    self.assertEqual(kwargs["revision"], "reviewed-revision")
                    self.assertEqual(kwargs["timeout"], 7)
                    self.assertEqual(kwargs["minimum_tests"], 2)
                    self.assertEqual(
                        kwargs["required_cases"],
                        frozenset({"ChosenSuite.first", "ChosenSuite.second"}),
                    )
                    self.assertEqual(
                        kwargs["environment"]["QT_QPA_PLATFORM"], "caller-platform"
                    )
                    self.assertEqual(
                        kwargs["environment"]["WIOLA_JOURNEY_MEDIA_DIR"],
                        str(media.resolve()),
                    )
                    self.assertEqual(dict(os.environ), inherited)
