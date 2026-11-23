import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('affected', Path(__file__).resolve().parents[2] / 'scripts/test-affected.py')
affected = importlib.util.module_from_spec(spec)
spec.loader.exec_module(affected)


class AffectedSelection(unittest.TestCase):
    def test_selects_wrapped_and_native_executables_and_escapes_nothing_implicitly(self):
        discovery = {'tests': [
            {'name': 'Foo.Case/1', 'command': ['wrapper', '/tmp/foo_test', '--flag']},
            {'name': 'Foo.Native', 'command': [r'C:\build\foo_test.exe']},
            {'name': 'Unrelated', 'command': ['/tmp/bar_test']}]}
        self.assertEqual(affected.select_tests(discovery, ['foo_test']), ['Foo.Case/1', 'Foo.Native'])

    def test_missing_or_empty_selection_is_an_error(self):
        with self.assertRaises(ValueError):
            affected.select_tests({'tests': []}, ['foo_test'])
        with self.assertRaises(ValueError):
            affected.select_tests({'tests': []}, [])

    def test_duplicate_discovery_is_not_a_valid_selection(self):
        case = {'name': 'Foo.Case', 'command': ['foo_test']}
        with self.assertRaises(ValueError):
            affected.select_tests({'tests': [case, case]}, ['foo_test'])


if __name__ == '__main__':
    unittest.main()
