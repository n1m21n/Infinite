#!/usr/bin/env python3
"""Website hygiene: spacing checks. See checks.py for the rules; run_all.py runs every family."""
import sys
from common import run
from checks import FAMILIES_MAP

if __name__ == "__main__":
    sys.exit(run(FAMILIES_MAP["spacing"], "website spacing checks", want_load=False))
