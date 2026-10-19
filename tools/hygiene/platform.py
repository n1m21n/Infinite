#!/usr/bin/env python3
"""Website hygiene: platform check. Renders each page at phone, tablet and desktop widths and reports pictures,
screenshots, canvases, code blocks and tables that overflow or do not fit, and images shown distorted."""
import sys
from common import run
from checks import FAMILIES_MAP

if __name__ == "__main__":
    sys.exit(run(FAMILIES_MAP["platform"], "website platform check"))
