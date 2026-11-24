#!/bin/bash
# Put a pre-commit launcher in the shared hooks folder (git rev-parse --git-common-dir)/hooks, which every worktree
# uses. The launcher runs the committing worktree's own scripts/git-hooks/pre-commit, so a branch gets the hook its
# tree carries (an old branch without one commits as before). core.hooksPath is left unset on purpose: setting it
# would switch off the post-commit / post-merge hooks already in .git/hooks. An existing pre-commit is never replaced.
set -euo pipefail
HOOKS="$(git rev-parse --git-common-dir)/hooks"
DST="$HOOKS/pre-commit"
MARK="# infinite pre-commit launcher"
if [[ -e "$DST" ]] && ! grep -q "$MARK" "$DST"; then
  echo "install: $DST exists and is not the launcher; left as is" >&2; exit 1
fi
cat > "$DST" <<'HOOK'
#!/bin/bash
# infinite pre-commit launcher (scripts/git-hooks/install.sh)
H="$(git rev-parse --show-toplevel)/scripts/git-hooks/pre-commit"
[[ -x "$H" ]] || exit 0
exec "$H" "$@"
HOOK
chmod +x "$DST"
echo "install: $DST -> <worktree>/scripts/git-hooks/pre-commit"
