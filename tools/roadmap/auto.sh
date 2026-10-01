#!/bin/bash
# Keeps the roadmap board current with no manual step: closes quests for merged branches, then rebuilds the page
# (live stars/downloads, daily metrics snapshot, GA when the key exists). Safe to run any time and from any cwd.
# Triggers: git post-merge hook (immediate) and launchd com.infinite.roadmap (hourly). Log: tools/roadmap/auto.log
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT" || exit 0
LOCK=/tmp/infinite-roadmap.lock
mkdir "$LOCK" 2>/dev/null || exit 0
trap 'rmdir "$LOCK"' EXIT
{
  echo "== $(date '+%F %T')"
  python3 tools/roadmap/backfill.py 2>&1 | tail -3
  python3 tools/roadmap/roadmap.py build 2>&1 | tail -2
} >> tools/roadmap/auto.log 2>&1
