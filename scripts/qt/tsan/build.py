"""Build the native Core target and admit only instrumented matching artifacts."""

from pathlib import Path
import re
import subprocess
import os

from tools.architecture.cmake_inventory.inventory import CMakeInventory
from tools.build_environment.cmake.build import CMakeBuild

from scripts.qt.source.provider import QtSourceStore

from .context import QtCoreContext
from .runtime import QtCoreRuntime


class QtCoreTsanBuilder:
    def __init__(
        self,
        workspace: Path,
        context: QtCoreContext,
        *,
        jobs: int | None = None,
        source_store: QtSourceStore | None = None,
        timeout: float = 900
    ):
        if timeout <= 0:
            raise ValueError("SDK build timeout must be positive")
        self.workspace = workspace.resolve()
        self.context, self.jobs = context, jobs
        self.runtime = QtCoreRuntime(self.workspace / "runtime")
        self.source_store = QtSourceStore() if source_store is None else source_store
        self.timeout = timeout

    def prepare(self) -> Path:
        if self.runtime.qualified(self.context.identity):
            return self.runtime.directory
        source = self._source()
        build = self.workspace / "build" / self.context.build_identity
        query = build / ".cmake/api/v1/query/codemodel-v2"
        query.parent.mkdir(parents=True, exist_ok=True)
        query.touch()
        with (self.workspace / "configure.log").open("w", encoding="utf-8") as log:
            subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(source),
                    "-B",
                    str(build),
                    "-DCMAKE_CXX_COMPILER=" + self.context.cxx,
                    "-DCMAKE_C_COMPILER=" + self.context.cc,
                    "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
                    "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O1 -g",
                    "-DCMAKE_C_FLAGS_RELWITHDEBINFO=-O1 -g",
                    "-DFEATURE_sanitize_thread=ON",
                    "-DFEATURE_glib=ON",
                    "-DQT_BUILD_TESTS=OFF",
                    "-DQT_BUILD_EXAMPLES=OFF",
                ],
                stdout=log,
                stderr=subprocess.STDOUT,
                check=True,
                timeout=self.timeout,
            )
        inventory = CMakeInventory.read(build)
        targets = [target for target in inventory.targets if target.name == "Core"]
        if (
            len(targets) != 1
            or targets[0].kind != "SHARED_LIBRARY"
            or len(targets[0].artifacts) != 1
        ):
            raise ValueError("Qt Core has no unique configured shared-library artifact")
        adapter = CMakeBuild(build, inventory, jobs=self.jobs, timeout=self.timeout)
        adapter.build("Core", self.workspace / "build.log")
        library = targets[0].artifacts[0]
        symbols = subprocess.check_output(["nm", "-D", str(library)], text=True)
        if not re.search(r"\b__tsan_", symbols):
            raise ValueError("Qt Core artifact has no TSan instrumentation")
        self.runtime.publish(library, self.context.identity)
        return self.runtime.directory

    def _source(self) -> Path:
        selected = os.environ.get("WIOLA_QT_SOURCE_CACHE")
        workspace = Path(selected) if selected else self.workspace
        return self.source_store.materialize(self.context.source, workspace)
