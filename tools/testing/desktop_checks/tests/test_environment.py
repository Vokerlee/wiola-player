from contextlib import redirect_stderr
from io import StringIO
from pathlib import Path
import tempfile
import unittest

from tools.testing.desktop_checks.environment import DesktopEnvironment
from tools.testing.desktop_checks.__main__ import main


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
