#!/usr/bin/env python3
"""Compatibility entry point for the J414s Mu profile manifest."""

import sys
from pathlib import Path

_TOOLS_DIR = str(Path(__file__).resolve().parent)
if _TOOLS_DIR not in sys.path:
    sys.path.insert(0, _TOOLS_DIR)

from mu_profile_manifest import *  # noqa: F401,F403


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "seal" and "--target" not in sys.argv:
        sys.argv[2:2] = ["--target", "j414s"]
    raise SystemExit(main())
