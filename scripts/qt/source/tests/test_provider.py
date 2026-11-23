from concurrent.futures import ThreadPoolExecutor
from contextlib import redirect_stderr, redirect_stdout
import errno
import hashlib
from io import BytesIO, StringIO
import json
import os
from pathlib import Path
import subprocess
import shutil
import sys
import tarfile
import tempfile
import threading
import unittest
from unittest.mock import patch

from scripts.qt.source.provider import QtSourcePin, QtSourceStore
from scripts.qt.source.__main__ import main


def archive_fixture(*, unsafe=False, missing_root=False):
    output = BytesIO()
    root = "wrong-root" if missing_root else "qtbase-everywhere-src-6.4.2"
    with tarfile.open(fileobj=output, mode="w:xz") as archive:
        for name, content in [
            (root + "/CMakeLists.txt", b"# reviewed fixture\n"),
            (root + "/value.cpp", b"void value() {}\n"),
        ]:
            info = tarfile.TarInfo(name)
            info.size = len(content)
            info.mode = 0o644
            archive.addfile(info, BytesIO(content))
        if unsafe:
            info = tarfile.TarInfo("../escape")
            info.size = 1
            archive.addfile(info, BytesIO(b"x"))
        else:
            empty = tarfile.TarInfo(root + "/empty")
            empty.type = tarfile.DIRTYPE
            archive.addfile(empty)
            link = tarfile.TarInfo(root + "/linked.cpp")
            link.type, link.linkname = tarfile.SYMTYPE, "value.cpp"
            archive.addfile(link)
    return output.getvalue()


class QtSourceStoreTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.archive = archive_fixture()
        self.pin = QtSourcePin("6.4.2", hashlib.sha256(self.archive).hexdigest())
        self.downloads = []

    def download(self, url, destination):
        self.downloads.append(url)
        destination.write_bytes(self.archive)

    def test_verified_tree_is_reused_without_downloading_again(self):
        store = QtSourceStore(self.download)
        source = store.materialize(self.pin, self.root)
        self.assertEqual((source / "value.cpp").read_bytes(), b"void value() {}\n")
        self.assertTrue((source / "linked.cpp").is_symlink())
        self.assertEqual(store.materialize(self.pin, self.root), source)
        self.assertEqual(self.downloads, [self.pin.url])
        self.assertFalse(list(self.root.rglob("*.part")))

    def test_edited_trees_and_corrupt_archives_are_rejected_without_overwriting(self):
        store = QtSourceStore(self.download)
        source = store.materialize(self.pin, self.root)
        (source / "value.cpp").write_text("external edit")
        with self.assertRaisesRegex(ValueError, "edited"):
            store.materialize(self.pin, self.root)
        self.assertEqual((source / "value.cpp").read_text(), "external edit")
        (self.root / (self.pin.directory + ".json")).unlink()
        with self.assertRaisesRegex(ValueError, "differs"):
            store.materialize(self.pin, self.root)
        archive = self.root / "download" / self.pin.archive
        archive.write_bytes(b"corrupt")
        with self.assertRaisesRegex(ValueError, "checksum"):
            store.materialize(self.pin, self.root)

    def test_identical_legacy_tree_is_adopted_and_invalid_receipt_can_be_repaired(self):
        with tarfile.open(fileobj=BytesIO(self.archive)) as archive:
            archive.extractall(self.root, filter="data")
        source = self.root / self.pin.directory
        original_inode = (source / "value.cpp").stat().st_ino
        receipt = self.root / (self.pin.directory + ".json")
        receipt.write_text("[]")
        store = QtSourceStore(self.download)
        self.assertEqual(store.materialize(self.pin, self.root), source)
        self.assertEqual((source / "value.cpp").stat().st_ino, original_inode)
        self.assertEqual(
            json.loads(receipt.read_text())["source_identity"], self.pin.identity
        )

    def test_failed_transfer_and_bad_checksums_never_admit_an_archive(self):
        def failed(url, path):
            path.write_bytes(b"partial")
            raise OSError("transfer failed")

        with self.assertRaises(OSError):
            QtSourceStore(failed).materialize(self.pin, self.root)
        with self.assertRaisesRegex(ValueError, "checksum"):
            QtSourceStore(lambda url, path: path.write_bytes(b"wrong")).materialize(
                self.pin, self.root
            )
        self.assertFalse((self.root / "download" / self.pin.archive).exists())
        self.assertFalse(list(self.root.rglob("*.part")))

    def test_unsafe_or_wrong_root_archives_cannot_publish_sources(self):
        for unsafe, missing in ((True, False), (False, True)):
            archive = archive_fixture(unsafe=unsafe, missing_root=missing)
            pin = QtSourcePin("6.4.2", hashlib.sha256(archive).hexdigest())
            workspace = self.root / str(unsafe)
            with self.assertRaises((ValueError, tarfile.TarError)):
                QtSourceStore(lambda url, path: path.write_bytes(archive)).materialize(
                    pin, workspace
                )
            self.assertFalse((workspace / pin.directory).exists())
            self.assertFalse((self.root / "escape").exists())

    def test_concurrent_publishers_only_admit_identical_trees(self):
        barrier = threading.Barrier(2)

        def download(url, path):
            barrier.wait(timeout=5)
            path.write_bytes(self.archive)

        with ThreadPoolExecutor(max_workers=2) as pool:
            futures = [
                pool.submit(QtSourceStore(download).materialize, self.pin, self.root)
                for _ in range(2)
            ]
            self.assertEqual(
                futures[0].result(timeout=10), futures[1].result(timeout=10)
            )
        self.assertEqual(
            QtSourceStore(self.download).materialize(self.pin, self.root),
            self.root / self.pin.directory,
        )
        self.assertEqual(self.downloads, [])

    def test_registry_and_cli_identity_reject_unreviewed_versions(self):
        pin = QtSourcePin.resolve("6.4.2")
        self.assertEqual(len(pin.identity), 64)
        with self.assertRaises(ValueError):
            QtSourcePin.resolve("6.99.0")
        for version, checksum in (("../escape", "0" * 64), ("6.4.2", "invalid")):
            with self.assertRaises(ValueError):
                QtSourcePin(version, checksum)
        result = subprocess.run(
            [sys.executable, "-m", "scripts.qt.source", "6.4.2", "--identity"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), pin.identity)

    def test_cli_uses_the_provider_and_reports_invalid_requests_without_tracebacks(
        self,
    ):
        archive = self.root / "download" / self.pin.archive
        archive.parent.mkdir()
        archive.write_bytes(self.archive)
        output = StringIO()
        with patch.object(
            QtSourcePin, "resolve", return_value=self.pin
        ), redirect_stdout(output):
            self.assertEqual(main([self.pin.version, str(self.root)]), 0)
        self.assertEqual(output.getvalue().strip(), str(self.root / self.pin.directory))
        output = StringIO()
        with redirect_stdout(output):
            self.assertEqual(main(["6.4.2", "--identity"]), 0)
        self.assertEqual(
            output.getvalue().strip(), QtSourcePin.resolve("6.4.2").identity
        )
        error = StringIO()
        with redirect_stderr(error):
            self.assertEqual(main(["6.99.0", "--identity"]), 1)
            with self.assertRaises(SystemExit) as stopped:
                main(["6.4.2"])
        self.assertEqual(stopped.exception.code, 2)
        self.assertIn("Qt source preparation failed", error.getvalue())
        self.assertNotIn("Traceback", error.getvalue())

    def test_default_downloader_uses_one_bounded_external_transfer(self):
        binary = self.root / "bin"
        binary.mkdir()
        fixture = self.root / "fixture.tar.xz"
        fixture.write_bytes(self.archive)
        curl = binary / "curl"
        curl.write_text(
            "#!" + sys.executable + "\nimport json,os,pathlib,shutil,sys\n"
            "args = sys.argv[1:]\n"
            "pathlib.Path(os.environ['QT_SOURCE_COMMAND_LOG']).write_text(json.dumps(args))\n"
            "shutil.copyfile(os.environ['QT_SOURCE_FIXTURE_ARCHIVE'], args[args.index('-o') + 1])\n"
        )
        curl.chmod(0o755)
        log = self.root / "transfer.json"
        with patch.dict(
            os.environ,
            PATH=str(binary) + os.pathsep + os.environ["PATH"],
            QT_SOURCE_COMMAND_LOG=str(log),
            QT_SOURCE_FIXTURE_ARCHIVE=str(fixture),
        ):
            QtSourceStore().materialize(self.pin, self.root / "workspace")
        arguments = json.loads(log.read_text())
        self.assertIn(self.pin.url, arguments)
        for flag in (
            "--fail",
            "--retry",
            "--connect-timeout",
            "--speed-time",
            "--speed-limit",
        ):
            self.assertIn(flag, arguments)

    def test_losing_a_publication_race_only_accepts_an_identical_winner(self):
        def concurrent_winner(source, destination):
            shutil.copytree(source, destination, symlinks=True)
            raise OSError(errno.ENOTEMPTY, "another source-store user won")

        with patch.object(Path, "rename", autospec=True, side_effect=concurrent_winner):
            result = QtSourceStore(self.download).materialize(self.pin, self.root)
        self.assertEqual(result, self.root / self.pin.directory)
        with patch.object(
            Path,
            "rename",
            side_effect=PermissionError(errno.EACCES, "not a publication race"),
        ):
            with self.assertRaises(PermissionError):
                QtSourceStore(self.download).materialize(self.pin, self.root / "denied")

    def test_failed_temporary_creation_or_receipt_write_cannot_admit_partial_state(
        self,
    ):
        with patch(
            "scripts.qt.source.provider.tempfile.TemporaryDirectory",
            side_effect=PermissionError("unwritable cache"),
        ):
            with self.assertRaises(PermissionError):
                QtSourceStore(self.download).materialize(self.pin, self.root)
        download = self.root / "download"
        (download / self.pin.archive).write_bytes(self.archive)
        original_write = Path.write_text

        def denied_receipt(path, *args, **kwargs):
            if path.name == self.pin.directory + ".json":
                raise PermissionError("unwritable receipt")
            return original_write(path, *args, **kwargs)

        with patch.object(
            Path, "write_text", autospec=True, side_effect=denied_receipt
        ):
            with self.assertRaises(PermissionError):
                QtSourceStore(self.download).materialize(self.pin, self.root)
        self.assertFalse((self.root / (self.pin.directory + ".json")).exists())
        self.assertTrue((self.root / self.pin.directory / "CMakeLists.txt").is_file())
        self.assertFalse(list(self.root.glob(".qt-receipt-*")))
        self.assertEqual(
            QtSourceStore(self.download).materialize(self.pin, self.root),
            self.root / self.pin.directory,
        )

    @unittest.skipUnless(
        hasattr(os, "mkfifo"), "This host cannot create a special-file fixture"
    )
    def test_unsupported_special_files_cannot_become_valid_source_cache_contents(self):
        store = QtSourceStore(self.download)
        source = store.materialize(self.pin, self.root)
        os.mkfifo(source / "not-a-source-file")
        with self.assertRaisesRegex(ValueError, "Unsupported entry"):
            store.materialize(self.pin, self.root)
