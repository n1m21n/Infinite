---
name: repo-privacy-split
description: Explains and enforces the branch/repo structure that keeps not-yet-public work (currently the OSC/RemoteControl feature and the MCP server) out of the public n1m21n/Infinite repo. Use before any `git push`, before starting a new feature that shouldn't ship yet, when asked "where does this go", "is this public or private", "should I push this to origin", "set up a private branch for X", or when git remotes/branches for this repo seem confusing or need to be checked.
---

Paths below are relative to the repo root (`/Users/namansoni/infinite`),
not this skill directory.

## The structure

Three repos, two branches. Know which one you're on before you push.

| Repo | Visibility | Role |
|---|---|---|
| [`n1m21n/Infinite`](https://github.com/n1m21n/Infinite) | **PUBLIC** | The app. Only public-ready commits ever land here. |
| [`n1m21n/infinite-private`](https://github.com/n1m21n/infinite-private) | private | Backup destination for the `osc-feature` branch (currently: OSC nodes + JSON-RPC RemoteControl work). |
| [`n1m21n/infinite-mcp`](https://github.com/n1m21n/infinite-mcp) | private | The MCP server (`mcp-server/`) as its own standalone repo, with its own history. Holds `PRIVACY.md`, the canonical ledger. |

Local branches in `/Users/namansoni/infinite`:

- **`main`** — tracks `origin/main`. Everyday work. `git push` here goes
  straight to the public repo, no ceremony needed.
- **`osc-feature`** — tracks `private/main`. Where not-yet-public feature
  work happens. `git push` here goes to the private mirror.

`mcp-server/` is not a subdirectory of this repo's history at all — it's a
separate git repo living on disk inside `infinite/mcp-server/`, gitignored
from `main` (and should be from every branch) so it can never be swept up
by `git add -A`.

## Rules to apply

1. **Before starting new work, ask: is this public-ready?**
   - Yes (bug fix, UI polish, anything fine to ship as-is) → work on `main`.
   - No (experimental, half-built, or deliberately withheld like the OSC/
     remote-control feature) → work on `osc-feature`, or create a new
     similarly-tracked branch for a *different* private feature (see
     "Adding another private feature" below).

2. **Before any `git push`, check the current branch and its upstream:**

       git branch -vv

   Confirm the branch you're on tracks the remote you actually intend.
   `git push` alone follows the tracked upstream — that's the safety net,
   not a substitute for checking.

3. **Before merging `osc-feature` into `main` (i.e. shipping it publicly),**
   read `PRIVACY.md` in the `infinite-mcp` repo first — it's the ledger of
   what's currently withheld and why. Update it as part of the same change
   once the merge happens, since the entry no longer applies.

4. **Never push `mcp-server/` content to `origin`.** It has its own remote
   (`infinite-mcp`) and its own workflow — commit and push from inside
   `mcp-server/` itself, independent of the outer repo's branch you're on.

## Adding another private feature later

If a second feature needs the same treatment, don't reuse `osc-feature` for
unrelated work — branch a new one from `main` (or from `osc-feature` if it
depends on that work), give it a name describing the feature, and set its
upstream to `private`:

    git checkout -b <feature-name> main
    git push -u private <feature-name>

Record the new branch and what it holds in `infinite-mcp`'s `PRIVACY.md` so
the ledger stays accurate — don't let it silently drift out of date.

## Quick sanity check

Run this whenever the structure feels unclear:

    git branch -vv
    git remote -v
    git log origin/main..main --oneline   # what public is missing, if on main
    git log private/main..osc-feature --oneline   # what's unpushed on the private side
