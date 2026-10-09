# Block J2: first run, signing, update, About

Decision held: no paid Apple account. Nothing here was signed up for or published. External facts below come from memory, not a fresh lookup, and are marked.

## What changed
| Item | Change |
|---|---|
| DMG `Read Me First.txt` | Opens with the two-step version; adds the macOS 15 "Open Anyway" path (right-click Open no longer clears the warning there) |
| README (macOS, Windows) | Same wording; the helper script is step 2; SmartScreen text says why and that it asks once per download |
| About window | Adds build line (compile date, release or debug), links to Source, Licence, Third-party notices, Releases (wrap with the dialog) |
| `package.sh`, `package.ps1` | LICENSE and THIRD_PARTY_NOTICES now ship in the macOS app Resources (before signing) and the Windows folder; Linux already did |
| `INFINITE_OPENDIALOG=about` | Review hook for the About dialog |

## Audit
| Check | Result |
|---|---|
| Helper script | Executable bit set by `package.sh` after staging; asks before it does anything; touches only Infinite.app |
| Linux | `.desktop` entry, icon and MIME type are written by `tools/linux/package-appimage.sh`; the AppImage loses its executable bit when downloaded, README step 1 is `chmod +x` |
| Check for updates | Menu, Settings and About all open the same modal (version shown, Download opens the releases page in the browser) |
| **Phones home silently** | **Not met.** `Startup.cpp:1058` makes one GitHub Releases request on every launch (it drives the update dot). Nothing is sent but the request itself, but the owner's rule says never silently. Recommendation: a Settings switch, "Check for updates when Infinite starts", default on only if the owner accepts that; not changed here |

## Not done
| Item | Why |
|---|---|
| DMG window background with the arrow and "right-click, Open" | Needs a Finder-scripted layout on a mounted image (or `dmgbuild`, not installed). Not attempted; the text now sits beside the app in the DMG instead |
| Screenshots of every first-run screen on a clean macOS account | No clean account here |
| Windows and Linux About/update behaviour | Code is shared, not run there |

## Signing and package managers (report only, from memory)
| Option | Cost | Fit | Effort |
|---|---|---|---|
| SignPath Foundation | free for OSI-licensed public projects | MIT and a public repo qualify; signing happens in CI via their GitHub Action after an application is approved | one application plus a workflow step; SmartScreen reputation still builds over downloads |
| Azure Trusted Signing | roughly 10 USD a month | Cheap and CI friendly; identity validation needed, and availability by country has been limited | account, validation, one CI step |
| Homebrew cask | free | `brew install --cask` is one line for users; Homebrew has been tightening rules for apps that are not notarized, so check current policy before investing | a small cask file in a tap, updated per release |
| winget | free | Manifest pull request to microsoft/winget-pkgs with installer URL and SHA256; a zip build needs the portable layout | about half a day, then one PR per release |
