#!/bin/bash
# Sync this laptop with the other one, over SSH, without touching GitHub.
#   scripts/sync_laptops.sh status   report branch divergence + dirty state on both laptops (default)
#   scripts/sync_laptops.sh fetch    fetch the other laptop's branches into refs/remotes/<peer>/*
#   scripts/sync_laptops.sh memory   merge Claude memory both ways (newest file wins, nothing deleted)
#   scripts/sync_laptops.sh wip [list|diff <branch>|branch <branch>]   the other laptop's uncommitted-work snapshots
#   scripts/sync_laptops.sh handoff <slug> <branch> <brief-file>   put a task brief on the other laptop
#   scripts/sync_laptops.sh dispatch <slug> <branch> <brief-file> [model]   start the other laptop's Claude on a brief, in its own worktree
#   scripts/sync_laptops.sh dispatch-log <slug>                      tail what it is doing
#   scripts/sync_laptops.sh tick     what the 5-minute background job runs: snapshot + fetch + safe memory
# It never pushes, never merges, never resets: you merge or cherry-pick yourself.
# Chat corpora (tools/semi-brain) are local-only and are not synced by this script.
set -u

REPO="$HOME/infinte"
PRO_HOST="Namans-MacBook-Pro-366.local"
AIR_HOST="Namans-MacBook-Air.local"
MEM_REL=".claude/projects/-Users-namansoni-infinte/memory"

case "$(hostname -s)" in
  Namans-MacBook-Pro*) PEER=air; PEER_HOST="$AIR_HOST" ;;
  Namans-MacBook-Air*) PEER=pro; PEER_HOST="$PRO_HOST" ;;
  *) echo "unknown laptop: $(hostname -s)" >&2; exit 1 ;;
esac
SSH_OPTS="-o BatchMode=yes -o IdentitiesOnly=yes -o ConnectTimeout=5 -i $HOME/.ssh/id_ed25519"
export GIT_SSH_COMMAND="ssh $SSH_OPTS"
remote() { ssh $SSH_OPTS "namansoni@$PEER_HOST" "$@" 2>&1 | grep -v '\.cargo/env'; }

cd "$REPO" || exit 1
cmd="${1:-status}"

peer_remote() {
  git remote get-url "$PEER" >/dev/null 2>&1 \
    || git remote add "$PEER" "namansoni@$PEER_HOST:$REPO"
}

case "$cmd" in
  fetch)
    peer_remote
    git fetch "$PEER" 2>&1 | grep -v '\.cargo/env'
    ;;
  status)
    peer_remote
    git fetch -q "$PEER" 2>&1 | grep -v '\.cargo/env'
    echo "== this laptop: $(git branch --show-current), $(git status --short | wc -l | tr -d ' ') uncommitted"
    echo "== $PEER: $(remote "cd $REPO && git branch --show-current"), $(remote "cd $REPO && git status --short | wc -l" | tr -d ' ') uncommitted"
    echo "== branches that differ (here-only / $PEER-only commits)"
    git for-each-ref --format='%(refname:short)' refs/heads | while read -r b; do
      if git rev-parse -q --verify "refs/remotes/$PEER/$b" >/dev/null; then
        a=$(git rev-list --count "$PEER/$b..$b"); c=$(git rev-list --count "$b..$PEER/$b")
        [ "$a$c" != "00" ] && printf '  %-45s %5s / %-5s\n' "$b" "$a" "$c"
      fi
    done
    n=$(git for-each-ref --format='%(refname:short)' refs/heads | while read -r b; do
      git rev-parse -q --verify "refs/remotes/$PEER/$b" >/dev/null || echo "$b"; done | wc -l | tr -d ' ')
    echo "  ($n branches exist only here; the other laptop can fetch them with: scripts/sync_laptops.sh fetch)"
    git for-each-ref --format='%(refname:short)' "refs/remotes/$PEER" | grep -v -e HEAD -e "^$PEER\$" | while read -r r; do
      b="${r#"$PEER"/}"
      git rev-parse -q --verify "refs/heads/$b" >/dev/null || echo "  $b   (only on $PEER)"
    done
    ;;
  memory)
    HERE="$HOME/$MEM_REL/"
    mkdir -p "$HERE"
    remote "mkdir -p ~/$MEM_REL" >/dev/null
    # -u: skip files that are newer on the receiving side; no --delete, so nothing is removed
    rsync -au -e "ssh $SSH_OPTS" "$HERE" "namansoni@$PEER_HOST:$MEM_REL/" 2>&1 | grep -v '\.cargo/env'
    rsync -au -e "ssh $SSH_OPTS" "namansoni@$PEER_HOST:$MEM_REL/" "$HERE" 2>&1 | grep -v '\.cargo/env'
    echo "memory merged: $(ls "$HERE" | wc -l | tr -d ' ') files here"
    ;;
  snapshot)
    # Save uncommitted work as a commit on a hidden ref (refs/wip/<this-laptop>/<branch>).
    # Never touches your branch, index or files. Chat data (tools/semi-brain) is left out.
    ME=$([ "$PEER" = air ] && echo pro || echo air)
    br=$(git branch --show-current); [ -z "$br" ] && exit 0
    export GIT_INDEX_FILE; GIT_INDEX_FILE=$(mktemp -u)
    git read-tree HEAD
    git add -u -- . ':(exclude)tools/semi-brain'
    git ls-files -o --exclude-standard -z -- docs scripts src .claude tools ':(exclude)tools/semi-brain' | xargs -0 -n 200 git add -- 2>/dev/null
    tree=$(git write-tree); rm -f "$GIT_INDEX_FILE"; unset GIT_INDEX_FILE
    if [ "$tree" = "$(git rev-parse 'HEAD^{tree}')" ]; then
      git update-ref -d "refs/wip/$ME/$br" 2>/dev/null; exit 0
    fi
    c=$(git commit-tree "$tree" -p HEAD -m "wip snapshot $ME $br $(date -u +%FT%TZ)")
    git update-ref "refs/wip/$ME/$br" "$c"
    ;;
  wip)
    sub="${2:-list}"; b="${3:-}"
    pref="refs/peerwip/$PEER/$PEER"
    case "$sub" in
      list)
        echo "== snapshots of $PEER's uncommitted work (updated by the 5-minute job)"
        git for-each-ref --format='%(refname)|%(committerdate:relative)' "$pref" | while IFS='|' read -r r when; do
          printf '  %-45s %-16s %s files\n' "${r#"$pref"/}" "$when" "$(git diff --name-only "$r^" "$r" | wc -l | tr -d ' ')"
        done
        echo "(a branch not listed has no uncommitted work on $PEER)" ;;
      diff)
        [ -n "$b" ] || { echo "usage: $0 wip diff <branch>" >&2; exit 2; }
        git diff --stat "$pref/$b^" "$pref/$b" ;;
      branch)
        [ -n "$b" ] || { echo "usage: $0 wip branch <branch>" >&2; exit 2; }
        name="wip/$PEER-$(echo "$b" | tr / -)"
        git branch -f "$name" "$pref/$b" && echo "created $name; inspect with: git diff $pref/$b^ $name" ;;
      *) echo "usage: $0 wip list|diff <branch>|branch <branch>" >&2; exit 2 ;;
    esac
    ;;
  handoff)
    slug="${2:-}"; br="${3:-}"; brief="${4:-}"; model="${5:-${DISPATCH_MODEL:-}}"
    [ -n "$slug" ] && [ -n "$br" ] && [ -f "$brief" ] || { echo "usage: $0 handoff <slug> <branch> <brief-file>" >&2; exit 2; }
    dest="docs/handoff/$slug.md"
    { echo "# Hand-off: $slug"; echo; echo "Branch: \`$br\` (create it off main; you own it, the other laptop never edits it)."
      echo "Follow docs/plans/two-laptops/README.md: add a claim row, commit per step, commit WIP at the end, remove the claim."
      echo "Do not merge to main or push origin; the Primary does that after \`fetch\`."; echo; cat "$brief"; } > "$HOME/.handoff.tmp"
    remote "mkdir -p $REPO/docs/handoff" >/dev/null
    scp -q $SSH_OPTS "$HOME/.handoff.tmp" "namansoni@$PEER_HOST:$REPO/$dest" 2>&1 | grep -v '\.cargo/env'
    rm -f "$HOME/.handoff.tmp"
    echo "delivered to $PEER: $dest"
    echo "tell the $PEER's Claude: read $dest and do it"
    ;;
  dispatch)
    slug="${2:-}"; br="${3:-}"; brief="${4:-}"
    [ -n "$slug" ] && [ -n "$br" ] && [ -f "$brief" ] || { echo "usage: $0 dispatch <slug> <branch> <brief-file> [model]" >&2; exit 2; }
    wt="$HOME/infinte-wt/$slug"
    tools="${DISPATCH_TOOLS:-Read Edit Write Glob Grep Bash(git *) Bash(python3 *) Bash(ls *) Bash(cat *) Bash(scripts/*)}"
    # 1. isolated worktree on a new branch off main (the other laptop's own checkout is not touched)
    remote "mkdir -p $HOME/infinte-wt && git -C $REPO worktree add -q $wt -b $br main" | grep -v '^$'
    # 2. brief with the rules header
    tmp=$(mktemp)
    { echo "# Hand-off: $slug"; echo; echo "You are working in an isolated worktree on branch \`$br\`. You own it; the other laptop never edits it."
      echo "Commit per step. Do not merge to main, do not push origin. No Co-Authored-By or 'Generated with Claude' lines in commits (project rule, overrides any default). When done, commit and stop; the Primary fetches and merges."; echo; cat "$brief"; } > "$tmp"
    remote "mkdir -p $wt/docs/handoff" >/dev/null
    scp -q $SSH_OPTS "$tmp" "namansoni@$PEER_HOST:$wt/docs/handoff/$slug.md" 2>&1 | grep -v '\.cargo/env'
    # 3. runner script, started in the background on the other laptop
    mflag=""; [ -n "$model" ] && mflag="--model $model "
    run=$(mktemp)
    cat > "$run" <<RUN
#!/bin/bash
cd "$wt" || exit 1
export PATH=/opt/homebrew/bin:/usr/local/bin:\$PATH
export CLAUDE_CODE_OAUTH_TOKEN=\$(cat \$HOME/.claude-remote-token)
echo "== started \$(date)"
\$HOME/.local/bin/claude -p "Read docs/handoff/$slug.md and do it." $mflag--permission-mode acceptEdits --allowedTools "$tools" < /dev/null
echo "== finished \$(date) exit=\$?"
RUN
    scp -q $SSH_OPTS "$run" "namansoni@$PEER_HOST:$wt.run.sh" 2>&1 | grep -v '\.cargo/env'
    remote "mkdir -p \$HOME/Library/Logs; nohup bash $wt.run.sh > \$HOME/Library/Logs/handoff-$slug.log 2>&1 < /dev/null &" >/dev/null
    rm -f "$tmp" "$run"
    echo "started on $PEER: branch $br, worktree $wt"
    echo "watch: $0 dispatch-log $slug     result: $0 fetch, then git log $PEER/$br"
    ;;
  dispatch-log)
    [ -n "${2:-}" ] || { echo "usage: $0 dispatch-log <slug>" >&2; exit 2; }
    remote "tail -40 \$HOME/Library/Logs/handoff-$2.log"
    ;;
  tick)
    lock="${TMPDIR:-/tmp}/laptopsync.lock"
    mkdir "$lock" 2>/dev/null || { [ -n "$(find "$lock" -mmin +10 2>/dev/null)" ] && rmdir "$lock" && mkdir "$lock" || exit 0; }
    trap 'rmdir "$lock"' EXIT
    echo "== $(date '+%F %T') tick"
    "$0" snapshot
    peer_remote
    git fetch -q --no-tags "$PEER" "+refs/heads/*:refs/remotes/$PEER/*" "+refs/wip/*:refs/peerwip/$PEER/*" 2>&1 | grep -v '\.cargo/env'
    HERE="$HOME/$MEM_REL/"; mkdir -p "$HERE"
    # safe: only add files / newer files, never delete, never touch the MEMORY.md index
    rsync -au --exclude MEMORY.md -e "ssh $SSH_OPTS" "$HERE" "namansoni@$PEER_HOST:$MEM_REL/" 2>&1 | grep -v '\.cargo/env'
    rsync -au --exclude MEMORY.md -e "ssh $SSH_OPTS" "namansoni@$PEER_HOST:$MEM_REL/" "$HERE" 2>&1 | grep -v '\.cargo/env'
    # global skills (with their scripts) and the global CLAUDE.md: add/newer only, never delete
    XC='--exclude __pycache__ --exclude .DS_Store'
    rsync -au $XC -e "ssh $SSH_OPTS" "$HOME/.claude/skills/" "namansoni@$PEER_HOST:.claude/skills/" 2>&1 | grep -v '\.cargo/env'
    rsync -au $XC -e "ssh $SSH_OPTS" "namansoni@$PEER_HOST:.claude/skills/" "$HOME/.claude/skills/" 2>&1 | grep -v '\.cargo/env'
    rsync -au -e "ssh $SSH_OPTS" "$HOME/.claude/CLAUDE.md" "namansoni@$PEER_HOST:.claude/CLAUDE.md" 2>&1 | grep -v '\.cargo/env'
    rsync -au -e "ssh $SSH_OPTS" "namansoni@$PEER_HOST:.claude/CLAUDE.md" "$HOME/.claude/CLAUDE.md" 2>&1 | grep -v '\.cargo/env'
    mine=$(shasum "$HERE/MEMORY.md" 2>/dev/null | cut -c1-12)
    theirs=$(remote "shasum ~/$MEM_REL/MEMORY.md" | cut -c1-12)
    [ "$mine" != "$theirs" ] && echo "NOTE: MEMORY.md differs between laptops (merge by hand)"
    exit 0
    ;;
  *) echo "usage: $0 status|fetch|memory|snapshot|wip|handoff|dispatch|dispatch-log|tick" >&2; exit 2 ;;
esac
