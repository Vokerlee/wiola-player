"""Discover the actual compiler, host Qt and GLib context for runtime reuse."""

from dataclasses import asdict, dataclass
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

from scripts.qt.source.provider import QtSourcePin, file_digest as digest


def _capture(*command: str) -> str:
    return subprocess.check_output(command, text=True).strip()


def _program(name: str) -> str:
    path = shutil.which(name)
    if path is None:
        raise ValueError("Required executable not found: " + name)
    # Preserve clang++/clang invocation names; resolving that symlink changes driver behavior.
    return os.path.abspath(path)


@dataclass(frozen=True)
class QtCoreContext:
    source: QtSourcePin
    source_identity: str
    cxx: str
    cc: str
    cxx_version: str
    cc_version: str
    compiler_target: str
    qt_prefix: str
    host_core_sha256: str
    glib_version: str
    recipe_sha256: str
    environment: tuple[tuple[str, str], ...]
    qualification_sha256: str

    @property
    def build_identity(self) -> str:
        inputs = asdict(self)
        inputs.pop("qualification_sha256")
        return hashlib.sha256(
            json.dumps(inputs, sort_keys=True).encode("utf-8")
        ).hexdigest()

    @property
    def identity(self) -> str:
        return hashlib.sha256(
            (self.build_identity + self.qualification_sha256).encode("ascii")
        ).hexdigest()

    @classmethod
    def discover(cls, cxx: str | None = None, cc: str | None = None) -> "QtCoreContext":
        cxx = _program(cxx or os.environ.get("CXX") or "clang++")
        sibling = (
            Path(cxx)
            .name.replace("clang++", "clang")
            .replace("g++", "gcc")
            .replace("c++", "cc")
        )
        candidate = Path(cxx).with_name(sibling)
        cc = _program(
            cc
            or os.environ.get("CC")
            or (
                str(candidate)
                if candidate.is_file() and sibling != Path(cxx).name
                else "cc"
            )
        )
        version = _capture("pkg-config", "--modversion", "Qt6Core")
        owner = Path(__file__).parent
        source = QtSourcePin.resolve(version)
        recipe = digest(owner / "build.py")
        host_core = (
            Path(_capture("pkg-config", "--variable=libdir", "Qt6Core"))
            / "libQt6Core.so.6"
        )
        return cls(
            source,
            source.identity,
            cxx,
            cc,
            _capture(cxx, "--version"),
            _capture(cc, "--version"),
            _capture(cxx, "-dumpmachine"),
            _capture("pkg-config", "--variable=prefix", "Qt6Core"),
            digest(host_core),
            _capture("pkg-config", "--modversion", "glib-2.0"),
            recipe,
            tuple(
                (name, os.environ.get(name, ""))
                for name in (
                    "CFLAGS",
                    "CXXFLAGS",
                    "LDFLAGS",
                    "PKG_CONFIG_PATH",
                    "PKG_CONFIG_LIBDIR",
                    "PKG_CONFIG_SYSROOT_DIR",
                    "CMAKE_GENERATOR",
                    "CMAKE_PREFIX_PATH",
                    "CMAKE_TOOLCHAIN_FILE",
                )
            ),
            digest(owner / "runtime.py"),
        )
