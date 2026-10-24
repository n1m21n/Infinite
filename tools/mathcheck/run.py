#!/usr/bin/env python3
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mathcheck  # noqa: E402

sys.exit(mathcheck.run(sys.argv[1:] or None))
