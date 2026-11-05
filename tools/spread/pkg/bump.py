#!/usr/bin/env python3
"""Point the Homebrew cask and the winget manifests at a published release.

    python3 tools/spread/pkg/bump.py v1.1.0

Downloads the release's DMG and both Windows zips, hashes them, rewrites
homebrew/Casks/infinite.rb and writes winget/manifests/n/n1m21n/Infinite/<version>/ from the newest
existing version folder. Nothing is submitted: copy the cask into n1m21n/homebrew-infinite and the winget
folder into a winget-pkgs PR yourself.
"""
import datetime, hashlib, pathlib, re, shutil, subprocess, sys, tempfile

HERE = pathlib.Path(__file__).resolve().parent
ASSETS = {"dmg": "Infinite.dmg", "x64": "Infinite-windows-x64.zip", "arm64": "Infinite-windows-ARM64.zip"}


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    if len(sys.argv) != 2 or not re.fullmatch(r"v\d+\.\d+\.\d+", sys.argv[1]):
        sys.exit(__doc__)
    tag = sys.argv[1]
    ver = tag[1:]
    with tempfile.TemporaryDirectory() as tmp:
        for name in ASSETS.values():
            subprocess.run(["gh", "release", "download", tag, "--repo", "n1m21n/Infinite", "--pattern", name, "--dir", tmp], check=True)
        h = {k: sha(pathlib.Path(tmp) / v) for k, v in ASSETS.items()}

    cask = HERE / "homebrew/Casks/infinite.rb"
    t = cask.read_text()
    t = re.sub(r'version "[^"]+"', f'version "{ver}"', t, count=1)
    t = re.sub(r'sha256 "[0-9a-f]{64}"', f'sha256 "{h["dmg"]}"', t, count=1)
    cask.write_text(t)

    root = HERE / "winget/manifests/n/n1m21n/Infinite"
    older = [d for d in root.iterdir() if d.is_dir() and d.name != ver]
    src = max(older or [root / ver], key=lambda d: [int(x) for x in d.name.split(".")])
    dst = root / ver
    if src != dst:
        shutil.rmtree(dst, ignore_errors=True)
        shutil.copytree(src, dst)
    for f in dst.iterdir():
        t = f.read_text().replace(f"PackageVersion: {src.name}", f"PackageVersion: {ver}").replace(f"/{('v' + src.name)}/", f"/{tag}/")
        if f.name.endswith(".installer.yaml"):
            t = re.sub(r"ReleaseDate: .*", f"ReleaseDate: {datetime.date.today().isoformat()}", t)
            t = re.sub(r"(windows-x64\.zip\n\s+InstallerSha256: )\w+", lambda m: m.group(1) + h["x64"].upper(), t)
            t = re.sub(r"(windows-ARM64\.zip\n\s+InstallerSha256: )\w+", lambda m: m.group(1) + h["arm64"].upper(), t)
        f.write_text(t)
    print(f"cask {ver} sha256 {h['dmg'][:12]}…; winget {ver} from {src.name}: x64 {h['x64'][:12]}…, arm64 {h['arm64'][:12]}…")


main()
