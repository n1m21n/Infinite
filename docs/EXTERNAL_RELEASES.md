# External Releases & Package Managers

Inventory of everything Infinite ships or publishes externally **outside** the standard GitHub Release app binaries (`.dmg`, `.pkg`, `.exe`, `.zip`, `.AppImage`, `.tar.gz`).

---

## 1. Package Managers

| Channel | Package / ID | Repo / Source | Current Live Version | Version on `main` | Automation | Notes / Next Step |
|---|---|---|---:|---:|---|---|
| **Homebrew** (macOS) | `n1m21n/homebrew-tap` (`Casks/infinite.rb`) | `n1m21n/homebrew-tap` | `2.0.0` | `2.0.0` | `.github/workflows/update-package-managers.yml` (`TAP_TOKEN`) | Automated on every published GitHub release. |
| **Winget** (Windows) | `n1m21n.Infinite` | `microsoft/winget-pkgs` (`tools/spread/pkg/winget/`) | PR `#483875` open | `2.0.0` | `.github/workflows/update-package-managers.yml` (`WINGET_TOKEN` + `winget-releaser@v2`) | Initial PR submitted (`microsoft/winget-pkgs#483875`). Once merged, future releases auto-update. |
| **npm** (MCP Server) | `infinite-engine` | `tools/npm/infinite-engine/` | `1.0.1` | `2.0.0` | `.github/workflows/publish-packages.yml` (`target=npm`, `NPM_TOKEN`) | Needs `NPM_TOKEN` added to GitHub repo secrets (or `npm login && npm publish --access public` locally). |
| **PyPI** (Python SDK) | `infinite-daw` | `tools/python_sdk/` | `2.0.0` | `2.0.0` | `.github/workflows/publish-packages.yml` (`target=pypi`, OIDC Trusted Publisher) | Published via `publish-packages.yml` (`target=pypi`). |
| **AUR** (Arch Linux) | `infinite-bin` | `tools/spread/pkg/aur/PKGBUILD` | Not on AUR yet | `2.0.0` | Manual (`ssh://aur@aur.archlinux.org/infinite-bin.git`) | `PKGBUILD` updated to `2.0.0`. Requires adding your SSH public key to `aur.archlinux.org` and pushing `PKGBUILD` + `.SRCINFO`. |

---

## 2. AI / MCP Registries & Plugins

| Channel | Identifier | Source | Version | Notes |
|---|---|---|---:|---|
| **Official MCP Registry** | `io.github.n1m21n/infinite-engine` | `tools/npm/infinite-engine/server.json` | `2.0.0` | References npm package `infinite-engine` (`stdio` transport). |
| **Claude Plugin / Standalone MCP** | `infinite-mcp-repo` | `/Users/namansoni/Infinite/infinite-mcp-repo` | — | Standalone Python MCP server (`server.py`, `.mcp.json`, `.claude-plugin/`). |

---

## 3. Web, Content & Community Distribution

| Channel | Source / Workflow | Notes |
|---|---|---|
| **Website (`infinitedevices.org`)** | `website/`, `.github/workflows/deploy-pages.yml` | Includes landing pages, `library.html`, `samples/`, `artworks.html`, `releases.html`, and interactive WebGL/WebAudio `embeds/`. |
| **Blog / SEO Posts** | `tools/seo/posts/`, `website/blog/`, `.github/workflows/blog-schedule.yml` | Scheduled publishing to `infinitedevices.org/blog/`. |
| **Spread / Growth Tooling** | `tools/spread/` | `radar.py`, `shader_porter.py`, `embed_gen.py`, `badge_gen.py`, `stream_bot.py` (manual-approval / draft-only workflows). |

---

## Quick Release Checklist (After a GitHub Release Tag `vX.Y.Z`)

1. **Homebrew & Winget:** Triggered automatically by `update-package-managers.yml` on `release: published`.
2. **npm (`infinite-engine`) & PyPI (`infinite-daw`):** Bump versions in `tools/npm/infinite-engine/{package.json,server.json}` and `tools/python_sdk/{setup.py,infinite/__init__.py}`, push to `main`, and run:
   ```bash
   gh workflow run publish-packages.yml --repo n1m21n/Infinite --ref main -f target=both
   ```
3. **AUR (`infinite-bin`):** Run `tools/spread/pkg/bump.py` or update `tools/spread/pkg/aur/PKGBUILD` (`pkgver` + `sha256sums_x86_64`), regenerate `.SRCINFO` with `makepkg --printsrcinfo > .SRCINFO`, and push to `ssh://aur@aur.archlinux.org/infinite-bin.git`.
