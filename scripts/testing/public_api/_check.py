"""One bounded install/relocate/compile/link protocol with retained diagnostics."""

from importlib.resources import files
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from scripts.build.cmake import CMakeBuild, CMakeInventory
from ._probe import PackageProbe


class QualificationError(RuntimeError):
    """A public consumer or precise private-header rejection did not qualify."""


class PublicConsumerCheck:
    def __init__(
        self,
        inventory: CMakeInventory,
        *,
        compiler: str,
        system: str,
        toolchain: Path | None = None,
        cmake: tuple[str, ...] = ("cmake",),
        jobs: int | None = None,
        timeout: float = 180,
    ):
        # Use the same single configured context and execution validation as other build tools.
        self.build = CMakeBuild(inventory, command=cmake, jobs=jobs, timeout=timeout)
        self.compiler = compiler
        self.system = system
        self.toolchain = (
            toolchain.resolve(strict=True) if toolchain is not None else None
        )

    def check(self, probe: PackageProbe, artifacts: Path, *, run: bool = False) -> None:
        artifacts.mkdir(parents=True, exist_ok=True)
        if any(artifacts.iterdir()):
            raise ValueError(
                "Consumer qualification artifacts must be an empty directory"
            )
        with tempfile.TemporaryDirectory(prefix="wiola-public-consumer-") as directory:
            work = Path(directory).resolve()
            if work.is_relative_to(self.build.inventory.source_root):
                raise ValueError(
                    "Public consumer workspace must be outside the source checkout"
                )
            prefix = work / "initial prefix"
            for index, component in enumerate(probe.components):
                self._execute(
                    [
                        "--install",
                        str(self.build.directory),
                        "--component",
                        component,
                        "--prefix",
                        str(prefix),
                    ],
                    artifacts / f"install-{index}.log",
                )
            relocated = work / "relocated prefix"
            prefix.rename(relocated)
            source = work / "consumer source"
            source.mkdir()
            source.joinpath("CMakeLists.txt").write_text(
                files(__package__)
                .joinpath("consumer.cmake")
                .read_text(encoding="utf-8"),
                encoding="utf-8",
            )
            shutil.copyfile(probe.consumer, source / "consumer.cpp")
            (source / "headers.txt").write_text(
                "\n".join(probe.headers) + "\n", encoding="utf-8"
            )
            (source / "private.txt").write_text(
                "\n".join(probe.private_headers) + "\n", encoding="utf-8"
            )
            configured = work / "consumer build"
            arguments = [
                "-S",
                str(source),
                "-B",
                str(configured),
                "-DCMAKE_CXX_COMPILER=" + self.compiler,
                "-DCMAKE_SYSTEM_NAME=" + self.system,
                "-DCMAKE_PREFIX_PATH=" + str(relocated),
                "-DWIOLA_PUBLIC_PACKAGE=" + probe.package,
                "-DWIOLA_PUBLIC_TARGET=" + probe.target,
            ]
            if self.toolchain is not None:
                arguments.append("-DCMAKE_TOOLCHAIN_FILE=" + str(self.toolchain))
            self._execute(arguments, artifacts / "configure.log")
            consumer = CMakeBuild(
                CMakeInventory.read(configured),
                command=self.build.command,
                jobs=self.build.jobs,
                timeout=self.build.timeout,
            )
            consumer.build("public_consumer", artifacts / "public.log")
            consumer.build("public_headers", artifacts / "headers.log")
            if run:
                with (artifacts / "run.log").open("w", encoding="utf-8") as output:
                    subprocess.run(
                        [str(consumer.executable("public_consumer"))],
                        cwd=work,
                        stdout=output,
                        stderr=subprocess.STDOUT,
                        check=True,
                        timeout=self.build.timeout,
                    )
            for index, header in enumerate(probe.private_headers):
                log = artifacts / f"private-{index}.log"
                try:
                    consumer.build(f"private_{index}", log)
                except subprocess.CalledProcessError:
                    diagnostic = log.read_text(encoding="utf-8")
                    # A header mention alone also occurs in unrelated syntax/link errors.
                    missing = re.search(
                        r"^[^\n]*?(?::[0-9]+(?::[0-9]+)?:|\([0-9]+(?:,[0-9]+)?\):)"
                        r"\s*(?:fatal error:|fatal error C1083:)\s*(?:Cannot open include file:\s*)?"
                        + r"['\"<]?"
                        + re.escape(header)
                        + r"['\">]?:?\s*(?:file not found|No such file or directory)",
                        diagnostic,
                        re.IGNORECASE | re.MULTILINE,
                    )
                    if not missing:
                        raise QualificationError(
                            "Private probe failed for an unrelated reason: " + header
                        )
                else:
                    raise QualificationError(
                        "Private header leaked into the public package: " + header
                    )
            shutil.copyfile(source / "consumer.cpp", artifacts / "consumer.cpp")
            (artifacts / "qualified.txt").write_text(
                f"Package: {probe.package}\nTarget: {probe.target}\n"
                + "Headers: "
                + ", ".join(probe.headers)
                + "\n"
                + "Rejected: "
                + ", ".join(probe.private_headers)
                + "\n",
                encoding="utf-8",
            )

    def _execute(self, arguments, log):
        with log.open("w", encoding="utf-8") as output:
            subprocess.run(
                [*self.build.command, *arguments],
                stdout=output,
                stderr=subprocess.STDOUT,
                check=True,
                timeout=self.build.timeout,
            )
