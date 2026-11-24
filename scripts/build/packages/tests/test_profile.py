import unittest

from scripts.build.packages._profile import host_packages


class HostProfileTest(unittest.TestCase):
    def test_existing_host_requirements_have_one_owner_and_keep_extras(self):
        self.assertEqual(host_packages("false", ""), ("ccache",))
        self.assertEqual(
            host_packages("true", "ccache llvm-18  llvm-18"),
            ("ccache", "qt6-base-dev", "qt6-qpa-plugins", "llvm-18"),
        )
        with self.assertRaisesRegex(ValueError, "true or false"):
            host_packages("unexpected", "")
