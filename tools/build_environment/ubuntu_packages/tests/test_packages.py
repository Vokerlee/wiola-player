import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from tools.build_environment.ubuntu_packages.packages import (
    PackageArchive,
    PackagePlan,
    UbuntuPackages,
)

URIS = """Reading package lists...
'http://mirror.example/pool/compiler.deb' compiler_13_amd64.deb 120 MD5Sum:abcdef
'http://mirror.example/pool/runtime.deb' runtime_13_amd64.deb 50 SHA256:123456
"""


class UbuntuPackagesTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.state = self.root / "state.json"
        self.log = self.root / "commands.jsonl"
        self.state.write_text(json.dumps({"uris": URIS, "failure": ""}))
        self.executable = self.root / "sudo"
        self.executable.write_text(
            "#!/usr/bin/env python3\n"
            "import json, pathlib, sys\n"
            "root = pathlib.Path(__file__).parent\n"
            "state = json.loads((root / 'state.json').read_text())\n"
            "args = sys.argv[1:]\n"
            "with (root / 'commands.jsonl').open('a') as output:\n"
            "    output.write(json.dumps(args) + '\\n')\n"
            "phase = 'update' if 'update' in args else ('plan' if '--print-uris' in args else 'install')\n"
            "if phase == state['failure']: sys.exit(23)\n"
            "if phase == 'plan':\n"
            "    cache = next(a.split('=', 1)[1] for a in args if a.startswith('Dir::Cache::archives='))\n"
            "    assert not list(pathlib.Path(cache).glob('*.deb'))\n"
            "    print(state['uris'])\n"
        )
        self.executable.chmod(0o755)
        self.manager = UbuntuPackages((sys.executable, str(self.executable)))

    def commands(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()]

    def fail_phase(self, phase):
        self.state.write_text(json.dumps({"uris": URIS, "failure": phase}))

    def test_identity_ignores_mirrors_and_request_order_but_tracks_resolved_bytes(self):
        plan = PackagePlan.from_uris(("zlib1g-dev", "ccache", "ccache"), URIS)
        other = PackagePlan.from_uris(
            ("ccache", "zlib1g-dev"),
            URIS.replace("http://mirror.example", "https://other.example"),
        )
        self.assertEqual(plan.identity, other.identity)
        for change in (
            URIS.replace("compiler_13", "compiler_14"),
            URIS.replace("abcdef", "abcdee"),
            URIS.replace("120 MD5", "121 MD5"),
            URIS.splitlines()[0] + "\n" + URIS.splitlines()[1],
        ):
            self.assertNotEqual(
                plan.identity, PackagePlan.from_uris(plan.packages, change).identity
            )
        self.assertNotEqual(
            plan.identity, PackagePlan.from_uris(("llvm",), URIS).identity
        )

    def test_resolver_uses_empty_archive_directory_and_round_trips_the_plan(self):
        plan = self.manager.resolve(("ccache", "g++-mingw-w64-x86-64-posix"))
        self.assertEqual(len(plan.archives), 2)
        commands = self.commands()
        self.assertIn("update", commands[0])
        self.assertIn("--print-uris", commands[1])
        self.assertIn("--no-install-recommends", commands[1])
        path = self.root / "plan.json"
        path.write_text(plan.serialize())
        self.assertEqual(PackagePlan.read(path), plan)
        self.assertEqual(plan.packages, ("ccache", "g++-mingw-w64-x86-64-posix"))

    def test_already_installed_packages_have_a_valid_empty_archive_plan(self):
        plan = PackagePlan.from_uris(
            ("ccache",), "ccache is already the newest version.\n"
        )
        self.assertEqual(plan.archives, ())
        self.assertEqual(len(plan.identity), 64)

    def test_invalid_requests_fail_before_any_privileged_command(self):
        for packages in ((), ("--allow-unauthenticated",), ("ccache;echo injected",)):
            with self.assertRaises(ValueError):
                self.manager.resolve(packages)
        self.assertFalse(self.log.exists())
        for request in ("ccache=4.9.1-1", "libc6:amd64"):
            self.assertEqual(PackagePlan((request,), ()).packages, (request,))

    def test_malformed_or_duplicate_archive_descriptions_fail(self):
        for value in (
            "'http://mirror/x' broken.deb 12",
            "'http://mirror/x' ../escape.deb 12 MD5Sum:aa",
            "'http://mirror/x' x.deb zero MD5Sum:aa",
            "'http://mirror/x' x.deb 0 MD5Sum:aa",
            "'http://mirror/x' x.deb 12 checksum-missing",
            URIS + URIS,
        ):
            with self.assertRaises(ValueError):
                PackagePlan.from_uris(("ccache",), value)
        with self.assertRaises(ValueError):
            PackageArchive("x.deb", True, "MD5Sum:aa")

    def test_install_retains_current_archives_and_removes_only_obsolete_top_level_debs(
        self,
    ):
        plan = PackagePlan.from_uris(("ccache",), URIS)
        cache = self.root / "cache with spaces"
        cache.mkdir()
        for name in (
            "compiler_13_amd64.deb",
            "runtime_13_amd64.deb",
            "old.deb",
            "note.txt",
        ):
            (cache / name).write_text("fixture")
        (cache / "nested").mkdir()
        (cache / "nested/old.deb").write_text("outside top-level cache scope")
        self.manager.install(plan, cache)
        self.assertEqual(
            {p.name for p in cache.iterdir()},
            {"compiler_13_amd64.deb", "runtime_13_amd64.deb", "note.txt", "nested"},
        )
        arguments = self.commands()[0]
        self.assertIn(f"Dir::Cache::archives={cache}", arguments)
        self.assertIn("APT::Keep-Downloaded-Packages=true", arguments)
        self.assertEqual(arguments[-2:], ["--", "ccache"])

    def test_failed_transactions_propagate_and_preserve_existing_archives(self):
        for phase in ("update", "plan"):
            self.fail_phase(phase)
            with self.assertRaises(subprocess.CalledProcessError):
                self.manager.resolve(("ccache",))
        self.fail_phase("install")
        cache = self.root / "archives"
        cache.mkdir()
        old = cache / "old.deb"
        old.write_text("must survive failed install")
        with self.assertRaises(subprocess.CalledProcessError):
            self.manager.install(PackagePlan(("ccache",), ()), cache)
        self.assertTrue(old.exists())

    def test_cli_identity_is_clean_and_failed_plans_do_not_publish_a_plan(self):
        environment = dict(
            os.environ, PATH=str(self.root) + os.pathsep + os.environ["PATH"]
        )
        plan = self.root / "cli.json"
        command = [
            sys.executable,
            "-m",
            "tools.build_environment.ubuntu_packages",
            "plan",
            "--output",
            str(plan),
            "--",
            "ccache",
        ]
        result = subprocess.run(
            command, env=environment, capture_output=True, text=True
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), PackagePlan.read(plan).identity)
        install = subprocess.run(
            [
                sys.executable,
                "-m",
                "tools.build_environment.ubuntu_packages",
                "install",
                "--plan",
                str(plan),
                "--archives",
                str(self.root / "cli archives"),
            ],
            env=environment,
            capture_output=True,
            text=True,
        )
        self.assertEqual(install.returncode, 0, install.stderr)
        plan.unlink()
        self.fail_phase("update")
        failed = subprocess.run(
            command, env=environment, capture_output=True, text=True
        )
        self.assertEqual(failed.returncode, 1)
        self.assertIn("Ubuntu package setup failed", failed.stderr)
        self.assertFalse(plan.exists())
