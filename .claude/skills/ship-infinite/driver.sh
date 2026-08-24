#!/usr/bin/env bash
# Driver for the ship-infinite skill: verify -> review -> commit -> push ->
# build DMG -> deploy via website/assets -> node-doc-change check -> cleanup.
# Run from the repo root, or this script will cd there itself.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$ROOT"

STEP="${1:-all}"

# ---------------------------------------------------------------- verify ---
step_verify() {
    echo "==> verify: running run-infinite-hygiene"
    if [ -x .claude/skills/run-infinite-hygiene/driver.sh ]; then
        .claude/skills/run-infinite-hygiene/driver.sh "${@:2}"
    else
        echo "    run-infinite-hygiene driver not found, doing a plain build instead"
        cmake --build build -j"$(sysctl -n hw.ncpu)"
    fi
}

# ----------------------------------------------------------------- review ---
step_review() {
    echo "==> review: uncommitted changes"
    if git diff --quiet && git diff --cached --quiet && [ -z "$(git status --porcelain --untracked-files=all)" ]; then
        echo "    working tree clean, nothing to review"
        return 1
    fi
    echo "--- git status ---"
    git status --short
    echo "--- git diff (staged + unstaged) ---"
    git --no-pager diff HEAD
    return 0
}

# ----------------------------------------------------------------- commit ---
step_commit() {
    local msg="${1:?commit message required}"
    echo "==> commit"
    git add -A
    if git diff --cached --quiet; then
        echo "    nothing staged, skipping commit"
        return 1
    fi
    git commit -m "$msg"
}

# ------------------------------------------------------------------- push ---
step_push() {
    echo "==> push"
    git push
}

# --------------------------------------------------------------- nodediff ---
# Reports node-catalog lines (REGISTER_NODE calls + the two help-text tables
# in src/main.cpp) that were added, removed, or changed between two refs.
# Usage: nodediff <base-ref> [head-ref=HEAD]
step_nodediff() {
    local base="${1:?base ref required, e.g. origin/main or a commit before your change}"
    local head="${2:-HEAD}"
    echo "==> nodediff: $base..$head (src/main.cpp node tables)"
    local pattern='REGISTER_NODE\(|^\s*\{ ".*", ".*" \},?\s*$'
    local changed
    changed=$(git diff -U0 "$base" "$head" -- src/main.cpp \
        | grep -E '^[+-]' | grep -v '^[+-][+-][+-]' \
        | grep -E "$pattern" || true)
    if [ -z "$changed" ]; then
        echo "    no node-catalog or help-text lines changed — manual likely doesn't need an update"
        return 1
    fi
    echo "    node-relevant lines changed in src/main.cpp:"
    echo "$changed" | sed 's/^/    /'
    echo
    echo "    ACTION NEEDED: review these against Infinite_Node_Reference_Manual.pdf"
    echo "    (manually maintained, not generated — this skill will not edit the PDF)."
    return 0
}

# ----------------------------------------------------------------- release ---
# Builds the universal Release .app + DMG via package.sh, which also copies
# the DMG into website/assets/Infinite.dmg. Slow (minutes, two-arch build).
# Then re-uploads the same DMG onto the latest GitHub Release as an asset -
# the website and the Releases page are two independent distribution
# points (a `git push` deploys the site; it does not touch a release's
# attached files), and a rebuild here means both need the new binary or the
# Releases page silently goes stale. Requires `gh` to be authenticated;
# skipped with a warning (not a hard failure - the website deploy is the
# part `commit`/`push` still cover) if it isn't.
step_release() {
    echo "==> release: package.sh (build + DMG + copy into website/assets)"
    ./package.sh

    echo "==> release: syncing website/assets/Infinite.dmg onto the latest GitHub Release"
    if ! command -v gh >/dev/null 2>&1; then
        echo "    gh CLI not found - skipping Release asset upload; website DMG is still up to date"
        return 0
    fi
    local tag
    tag=$(gh release view --json tagName -q .tagName 2>/dev/null)
    if [ -z "$tag" ]; then
        echo "    no GitHub Release found (or gh not authenticated) - skipping Release asset upload"
        return 0
    fi
    gh release upload "$tag" website/assets/Infinite.dmg --clobber
    echo "    uploaded website/assets/Infinite.dmg onto release $tag"

    step_release_windows "$tag"
}

# --------------------------------------------------------- release-windows ---
# Windows binaries can't be built on this (macOS) machine, so instead of
# compiling them this pulls the x64/ARM64 .exe already built by the
# `windows` job in .github/workflows/build.yml for the commit just pushed,
# repacks them to match the existing Release asset layout (an `Infinite/`
# folder with the exe + README + LICENSE, zipped), and uploads them onto
# the same GitHub Release the DMG just went to. Best-effort: a missing `gh`,
# a still-running/failed CI run, or a run with no Windows artifacts (e.g.
# both jobs failed) all just print a warning and return - they never fail
# the release step, since the DMG/website deploy already succeeded by the
# time this runs.
step_release_windows() {
    local tag="${1:?tag required}"
    echo "==> release: syncing Windows zips onto release $tag from CI"

    local sha
    sha=$(git rev-parse HEAD)
    local run_id
    run_id=$(gh run list --workflow=build.yml --branch main --status success \
        --json databaseId,headSha --limit 20 \
        --jq "[.[] | select(.headSha == \"$sha\")][0].databaseId" 2>/dev/null)
    if [ -z "$run_id" ] || [ "$run_id" = "null" ]; then
        echo "    no successful CI run found yet for $sha - skipping Windows asset sync"
        echo "    (CI may still be running; re-run 'release' once it's green, or upload manually)"
        return 0
    fi

    local work
    work=$(mktemp -d)
    local arch ok=0
    for arch in x64 ARM64; do
        local art="Infinite-windows-$arch"
        if ! gh run download "$run_id" -n "$art" -D "$work/$art" >/dev/null 2>&1; then
            echo "    $art artifact not found on run $run_id - skipping (job may have failed/continue-on-error)"
            continue
        fi
        local stage="$work/stage-$arch/Infinite"
        mkdir -p "$stage"
        cp "$work/$art"/* "$stage/" 2>/dev/null
        (cd "$work/stage-$arch" && zip -qr "$work/$art.zip" Infinite)
        gh release upload "$tag" "$work/$art.zip" --clobber
        echo "    uploaded $art.zip onto release $tag"
        ok=1
    done
    rm -rf "$work"
    if [ "$ok" -eq 0 ]; then
        echo "    no Windows artifacts uploaded - release $tag's Windows zips are unchanged"
    fi
}

# ----------------------------------------------------------------- cleanup ---
# Auto-deletes ONLY files matching unambiguous junk patterns — editor swap
# files, OS cruft, compiled artifacts. That's it.
#
# Byte-identical tracked duplicates are REPORTED, never auto-deleted. An
# earlier version of this script auto-deleted "unreferenced" duplicates
# (no tracked file's text mentions the basename) and, tested against a real
# clone of this repo, it deleted both copies of Infinite_Overview_Deck.pdf
# and The_Node_Field_Guide.pdf (linked from GitHub's own file browser, not
# from any tracked file's text) and every assets/*.iconset/*.png (consumed
# by `iconutil` by directory, never referenced by filename anywhere
# grep-able). "Not grep-referenced" does not mean "unused" — only a human
# knows that. Duplicates are surfaced so you can decide, never removed
# automatically.
step_cleanup() {
    echo "==> cleanup: scanning for junk-pattern files (auto-delete)"
    local junk
    junk=$(git ls-files -z | xargs -0 -I{} sh -c '
        case "{}" in
            *.orig|*.bak|*.swp|*~|*/Thumbs.db|*/__MACOSX/*|*.pyc|*.class) echo "{}" ;;
        esac
    ')
    if [ -n "$junk" ]; then
        echo "    junk-pattern files (deleting):"
        echo "$junk" | sed 's/^/    /'
        echo "$junk" | xargs -I{} git rm -q "{}"
    else
        echo "    no junk-pattern files tracked"
    fi

    echo "==> cleanup: scanning for byte-identical tracked duplicates (report only)"
    local tmp
    tmp=$(mktemp)
    git ls-files | grep -v '^external/' | while read -r f; do
        [ -f "$f" ] || continue
        printf '%s  %s\n' "$(shasum "$f" | cut -d' ' -f1)" "$f"
    done | sort > "$tmp"

    local found=0
    awk '{print $1}' "$tmp" | uniq -d | while read -r hash; do
        echo "    duplicate content:"
        grep "^$hash " "$tmp" | awk '{print $2}' | sed 's/^/      /'
    done
    rm -f "$tmp"
    echo "    (not deleted — review by hand: is each copy actually needed for a"
    echo "     different consumer, like the DMG stage vs. the website download link?)"

    if ! git diff --cached --quiet; then
        echo "    junk-pattern removals above are staged — commit them with the commit subcommand"
    else
        echo "    no junk-pattern files to delete"
    fi
}

# ------------------------------------------------------------------- all ---
case "$STEP" in
    verify) shift; step_verify "$@" ;;
    review) step_review ;;
    commit) shift; step_commit "$@" ;;
    push) step_push ;;
    nodediff) shift; step_nodediff "$@" ;;
    release) step_release ;;
    release-windows) shift; step_release_windows "$@" ;;
    cleanup) step_cleanup ;;
    all)
        echo "Use individual subcommands (verify/review/commit/push/nodediff/release/cleanup)."
        echo "See SKILL.md — 'all' is intentionally not a single unattended command because"
        echo "commit needs a message and nodediff needs a base ref."
        exit 1
        ;;
    *)
        echo "unknown step: $STEP" >&2
        exit 1
        ;;
esac
