#!/usr/bin/env python3
"""Run every website hygiene family in one browser pass: padding, spacing, typography, buttons, layout, tokens,
density, loading. Exits 1 on any error-level finding. Flags: --page, --viewport, --all, --limit, --json."""
import sys
from common import run
from checks import FAMILIES_MAP

if __name__ == "__main__":
    sys.exit(run([f for fs in FAMILIES_MAP.values() for f in fs], "website hygiene, all families", want_load=True))
