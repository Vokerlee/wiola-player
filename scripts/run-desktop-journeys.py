#!/usr/bin/env python3
"""Compatibility entrypoint for the owned packaged desktop checks."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.testing.desktop_checks.__main__ import main

if __name__ == "__main__":
    raise SystemExit(main())
