"""Verify a relocated installed API without any checkout include directories."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--system", required=True)
    args = parser.parse_args()
    fixture = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix="wiola-public-consumer-") as directory:
        temporary = Path(directory)
        prefix = temporary / "installed"
        subprocess.run([args.cmake, "--install", str(args.build_dir), "--component",
                        "wiola_borrowed_reference", "--prefix", str(prefix)], check=True)
        relocated = temporary / "relocated"
        prefix.rename(relocated)
        for index, header in enumerate(("core/handle.hpp", "tests/private_probe.hpp")):
            build = temporary / str(index)
            subprocess.run([args.cmake, "-S", str(fixture), "-B", str(build),
                            f"-DCMAKE_PREFIX_PATH={relocated}", f"-DCMAKE_CXX_COMPILER={args.compiler}",
                            f"-DCMAKE_SYSTEM_NAME={args.system}",
                            f"-DWIOLA_PRIVATE_HEADER={header}"], check=True)
            subprocess.run([args.cmake, "--build", str(build), "--target", "public_consumer"], check=True)
            private = subprocess.run([args.cmake, "--build", str(build), "--target", "private_consumer"],
                                     capture_output=True, text=True)
            if private.returncode == 0 or header not in private.stdout + private.stderr:
                raise RuntimeError(f"Private include did not fail for the expected header: {header}")


if __name__ == "__main__":
    main()
