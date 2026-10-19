#!/usr/bin/env python3
"""Website hygiene: buttons checks. See checks.py for the rules; run_all.py runs every family."""
import sys
from common import run
from checks import FAMILIES_MAP

if __name__ == "__main__":
    sys.exit(run(FAMILIES_MAP["buttons"], "website buttons checks", want_load=False))
