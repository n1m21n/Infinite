#!/usr/bin/env bash
# Синхронизация форка с апстримом. Коды: 0 ок, 1 грязная копия, 2 конфликт, 3 красные тесты.
set -u
cd "$(git rev-parse --show-toplevel)"
if [ -n "$(git status --porcelain --untracked-files=no | grep -v '^ m external/')" ]; then
  echo "Есть незакоммиченные изменения — закоммить или убери." >&2; exit 1
fi
git fetch upstream || exit 1
git checkout main || exit 1
git merge --ff-only upstream/main || { echo "main разошёлся с upstream/main" >&2; exit 1; }
git push origin main || exit 1
git checkout my/main || exit 1
if ! git merge --no-edit main; then
  echo "КОНФЛИКТЫ:" >&2
  git diff --name-only --diff-filter=U >&2
  exit 2
fi
git submodule update --init --recursive
scripts/ibuild.py build --mode dev || fail=1
if [ -z "${fail:-}" ]; then
  INFINITE_BIN=build-dev/Infinite.app/Contents/MacOS/Infinite \
    .claude/skills/run-infinite-hygiene/driver.sh --skip-build --fast || fail=1
fi
if [ -n "${fail:-}" ]; then
  br="sync/$(date +%Y-%m-%d)"
  git branch -f "$br" HEAD
  git reset --hard ORIG_HEAD
  echo "Красно: результат в $br, my/main не сдвинута." >&2
  exit 3
fi
git push origin my/main
