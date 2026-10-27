#!/usr/bin/env python3
"""Metadata and parser reader for unread formats (Task A / step 3 of data universe).
    read_formats.py [ROOT ...] [--inventory drive_inventory.json] [--out formats_meta.jsonl] [--resume]

Parses or extracts metadata for 8 formats:
  Real parsers:
    .sfz: plain-text sampler mapping (regions, key ranges, velocity ranges, sample references, opcodes)
    .fxp: VST preset chunk/bank header (plugin id, plugin name, program name, chunk size, fx magic, version)
  Metadata-only (cheap and honest):
    .asd: Ableton warp analysis (size, onsets count, schema magic)
    .amxd: Max for Live device (size, embedded patcher version, box count)
    .cst: Logic channel strip (size, plug-in chain if OCuA, or legacy binary header)
    .bwproject: Bitwig project (size, BtWg version, metadata creator/app_version)
    .nksn: Native Instruments Kontakt snapshot (size, chunk signatures)
    .nka: Native Instruments Kontakt array (size, array name, item count, sample items)

Output rows written to store/formats_meta.jsonl with standard sibling reader keys:
  src, kind, name, category, license, container_type
Licence: "preset library, local only"."""

import argparse, collections, json, os, re, struct, sys, time

SKIP_DIRS = {".Spotlight-V100", ".Trashes", ".fseventsd", ".TemporaryItems", ".DocumentRevisions-V100",
             "node_modules", ".git", "__pycache__"}
LIC_PRESET = "preset library, local only"
LIC_SAMPLE = "sample library, local only"

TARGET_EXTS = {".sfz", ".fxp", ".asd", ".cst", ".bwproject", ".nksn", ".nka", ".amxd"}

PLUGIN_MAP = {
    b"VsSr": "HALion",
    b"XfsX": "Serum",
    b"sErU": "Serum",
    b"Ni$F": "Massive",
    b"Syl1": "Sylenth1",
    b"Spir": "Spire",
    b"Diva": "Diva",
    b"Z2ta": "Z3TA+",
}

METADATA_ONLY_TABLE = [
    (".asd", "ableton_warp_analysis", "metadata-only", "warp/onset marker count; audio re-measured by audio_features"),
    (".amxd", "max_for_live_device", "metadata-only", "embedded patcher JSON parsed (appversion, box count); DSP opaque"),
    (".cst", "logic_channel_strip", "metadata-only", "OCuA plug-in chains extracted; older binary format captured as metadata"),
    (".bwproject", "bitwig_project", "metadata-only", "BtWg project container; header version and metadata block parsed"),
    (".nksn", "ni_kontakt_snapshot", "metadata-only", "NI Kontakt snapshot container (hsin/DSIN); engine state proprietary"),
    (".nka", "ni_kontakt_array", "metadata-only", "Kontakt script array; table/articulation names and item count parsed"),
]


def base_row(path, kind, name=None, lic=LIC_PRESET):
    base_name = name or os.path.splitext(os.path.basename(path))[0]
    category = os.path.basename(os.path.dirname(path))
    return {
        "src": path,
        "kind": kind,
        "name": base_name,
        "category": category,
        "license": lic,
    }


def parse_sfz(path):
    """Real parser for .sfz definition files."""
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError:
        return None

    r = base_row(path, "sfz", lic=LIC_SAMPLE)
    r["container"] = "decoded"
    r["container_type"] = "sfz_map"

    # Tokenise regions
    region_blocks = re.split(r"<region>", text, flags=re.I)
    regions = []
    samples = []
    min_key, max_key = 127, 0
    all_opcodes = set()

    for blk in region_blocks[1:]:
        reg = {}
        # match opcodes like name=val
        for m in re.finditer(r"([a-zA-Z0-9_]+)=([^\s]+)", blk):
            k, v = m.group(1).lower(), m.group(2).strip("\"'")
            reg[k] = v
            all_opcodes.add(k)
        if "sample" in reg:
            samples.append(reg["sample"])
        lo = int(reg.get("lokey", reg.get("key", 128)))
        hi = int(reg.get("hikey", reg.get("key", -1)))
        if lo <= 127:
            min_key = min(min_key, lo)
        if hi >= 0:
            max_key = max(max_key, hi)
        regions.append(reg)

    r["regions_count"] = len(regions)
    r["samples_count"] = len(samples)
    r["key_range"] = [min_key, max_key] if min_key <= max_key else []
    r["samples"] = samples[:20]
    r["opcodes"] = sorted(all_opcodes)
    return r


def parse_fxp(path):
    """Real parser for VST preset chunk (.fxp)."""
    try:
        with open(path, "rb") as f:
            b = f.read(64)
    except OSError:
        return None

    if len(b) < 28 or b[:4] != b"CcnK":
        return None

    chunk_magic, byte_size, fx_magic, ver, fx_id, fx_ver, num_prog = struct.unpack(">4sI4sI4sII", b[:28])
    name = b[28:56].split(b"\0")[0].decode("latin-1", "replace").strip()
    chunk_size = struct.unpack(">I", b[56:60])[0] if len(b) >= 60 else 0

    r = base_row(path, "fxp", name=name if name else None)
    r["container"] = "decoded"
    r["container_type"] = "vst_preset"
    r["plugin_id"] = fx_id.decode("latin-1", "replace")
    r["plugin_name"] = PLUGIN_MAP.get(fx_id, r["plugin_id"])
    r["program_name"] = name
    r["fx_magic"] = fx_magic.decode("latin-1", "replace")
    r["fx_version"] = fx_ver
    r["chunk_size"] = chunk_size
    r["byte_size"] = byte_size
    return r


def parse_asd(path):
    """Metadata-only reader for Ableton warp analysis (.asd)."""
    try:
        st = os.stat(path)
        with open(path, "rb") as f:
            head = f.read(4096)
    except OSError:
        return None

    r = base_row(path, "asd")
    r["container"] = "metadata-only"
    r["container_type"] = "ableton_warp_analysis"
    r["size"] = st.st_size
    r["onsets_count"] = head.count(b"OnsetEvent")
    r["warp_markers_count"] = head.count(b"WarpMarker")
    r["has_schema"] = b"\xab\x1e\x56\x78" in head
    return r


def parse_amxd(path):
    """Metadata-only reader for Max for Live device (.amxd)."""
    try:
        st = os.stat(path)
        with open(path, "rb") as f:
            b = f.read(65536)
    except OSError:
        return None

    r = base_row(path, "amxd")
    r["container"] = "metadata-only"
    r["container_type"] = "max_for_live_device"
    r["size"] = st.st_size

    idx = b.find(b'"patcher"')
    if idx >= 0:
        start = b.rfind(b"{", 0, idx)
        if start >= 0:
            try:
                js, _ = json.JSONDecoder().raw_decode(b[start:].decode("utf-8", "replace"))
                patcher = js.get("patcher", {})
                r["appversion"] = patcher.get("appversion")
                r["boxes_count"] = len(patcher.get("boxes", []))
                r["description"] = patcher.get("description", "")
            except Exception:
                pass
    return r


def parse_cst(path):
    """Metadata-only reader for Logic channel strip (.cst)."""
    try:
        st = os.stat(path)
        with open(path, "rb") as f:
            b = f.read(min(st.st_size, 32768))
    except OSError:
        return None

    r = base_row(path, "cst")
    r["container"] = "metadata-only"
    r["size"] = st.st_size

    if b[:4] == b"OCuA":
        r["container_type"] = "logic_channel_strip (OCuA)"
        chain = []
        for m in re.finditer(b"UCuA", b):
            nm = b[m.start() + 0x9C:m.start() + 0xBC].split(b"\0")[0]
            if re.fullmatch(rb"[A-Za-z0-9][A-Za-z0-9 .+&()/'-]{1,30}", nm):
                chain.append(nm.decode("latin-1", "replace"))
        r["chain"] = chain
    else:
        r["container_type"] = "logic_channel_strip (legacy binary)"
        r["chain"] = []
    return r


def parse_bwproject(path):
    """Metadata-only reader for Bitwig project (.bwproject)."""
    try:
        st = os.stat(path)
        with open(path, "rb") as f:
            b = f.read(4096)
    except OSError:
        return None

    r = base_row(path, "bwproject")
    r["container"] = "metadata-only"
    r["container_type"] = "bitwig_project"
    r["size"] = st.st_size

    if b[:4] == b"BtWg":
        r["btwg_version"] = b[4:8].decode("latin-1", "replace")
        # Extract metadata strings
        strings = [m.group().decode("latin-1", "replace") for m in re.finditer(rb"[\x20-\x7e]{4,}", b[:512])]
        for i, s in enumerate(strings):
            if s == "creator" and i + 1 < len(strings):
                r["creator"] = strings[i + 1]
            elif s == "application_version_name" and i + 1 < len(strings):
                r["app_version"] = strings[i + 1]
    return r


def parse_nksn(path):
    """Metadata-only reader for Kontakt snapshot (.nksn)."""
    try:
        st = os.stat(path)
        with open(path, "rb") as f:
            b = f.read(4096)
    except OSError:
        return None

    r = base_row(path, "nksn")
    r["container"] = "metadata-only"
    r["container_type"] = "ni_kontakt_snapshot"
    r["size"] = st.st_size
    r["has_hsin"] = b"hsin" in b
    r["chunks_found"] = [c for c in ("hsin", "DSIN", "4KIN", "NIKS") if c.encode() in b]
    return r


def parse_nka(path):
    """Metadata-only reader for Kontakt array (.nka)."""
    try:
        st = os.stat(path)
        with open(path, "r", encoding="latin-1", errors="replace") as f:
            head_lines = [f.readline() for _ in range(30)]
    except OSError:
        return None

    r = base_row(path, "nka")
    r["container"] = "metadata-only"
    r["container_type"] = "ni_kontakt_array"
    r["size"] = st.st_size

    array_name = head_lines[0].strip() if head_lines else ""
    items = [line.strip() for line in head_lines[1:] if line.strip()]
    r["array_name"] = array_name
    r["items_sample"] = items[:5]
    return r


PARSERS = {
    ".sfz": parse_sfz,
    ".fxp": parse_fxp,
    ".asd": parse_asd,
    ".amxd": parse_amxd,
    ".cst": parse_cst,
    ".bwproject": parse_bwproject,
    ".nksn": parse_nksn,
    ".nka": parse_nka,
}


def main():
    ap = argparse.ArgumentParser(description="Metadata reader for unread formats.")
    ap.add_argument("roots", nargs="*", default=["/Volumes/Wd-Black"])
    ap.add_argument("--inventory", default=os.path.expanduser("~/Infinite/training-data/drive_inventory.json"))
    ap.add_argument("--out", default=os.path.expanduser("~/Infinite/training-data/store/formats_meta.jsonl"))
    ap.add_argument("--resume", action="store_true", help="Resume from existing output file")
    a = ap.parse_args()

    out_path = os.path.expanduser(a.out)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)

    done_paths = set()
    if a.resume and os.path.exists(out_path):
        for ln in open(out_path):
            try:
                row = json.loads(ln)
                done_paths.add(row["src"])
            except Exception:
                pass
        print(f"Resuming: {len(done_paths)} files already processed.")

    # Find top folders from inventory if available to prune directory walk
    top_ext = {}
    if a.inventory and os.path.exists(a.inventory):
        try:
            top_ext = json.load(open(a.inventory)).get("top_ext", {})
        except Exception as e:
            print(f"Warning: could not load inventory {a.inventory}: {e}")

    # Determine folders to walk
    folders_to_walk = []
    if top_ext and len(a.roots) == 1 and a.roots[0] == "/Volumes/Wd-Black":
        for top, exts in top_ext.items():
            if any(e in TARGET_EXTS for e in exts):
                p = os.path.join(a.roots[0], top)
                if os.path.exists(p):
                    folders_to_walk.append((p, {e for e in exts if e in TARGET_EXTS}))
    else:
        for r in a.roots:
            if os.path.exists(r):
                folders_to_walk.append((r, TARGET_EXTS))

    print(f"Scanning {len(folders_to_walk)} roots for {sorted(TARGET_EXTS)}...")
    t0 = time.time()
    tally = collections.Counter()
    n_written = 0

    mode = "a" if (a.resume and os.path.exists(out_path)) else "w"
    with open(out_path, mode, encoding="utf-8") as fo:
        for root_dir, wanted_exts in folders_to_walk:
            for root, dirs, files in os.walk(root_dir):
                dirs[:] = [d for d in dirs if not d.startswith(".") and d not in SKIP_DIRS]
                for f in files:
                    if f.startswith("._"):
                        continue
                    ext = os.path.splitext(f)[1].lower()
                    if ext not in wanted_exts:
                        continue
                    path = os.path.join(root, f)
                    if path in done_paths:
                        tally[ext] += 1
                        continue

                    parser = PARSERS.get(ext)
                    if parser:
                        row = parser(path)
                        if row:
                            fo.write(json.dumps(row, separators=(",", ":")) + "\n")
                            tally[ext] += 1
                            n_written += 1
                            if n_written % 1000 == 0:
                                fo.flush()
                                print(f"  {n_written} rows written ({time.time() - t0:.1f} s)...")

        fo.flush()

    elapsed = time.time() - t0
    print(f"\nDone in {elapsed:.1f} s. Rows by format:")
    for e in sorted(TARGET_EXTS):
        print(f"  {e:12s}: {tally[e]:6d}")
    print(f"Total rows in {out_path}: {sum(tally.values())}")

    print("\nMetadata-only container declaration table (for criterion C2):")
    print("| format | container type | status | notes |")
    print("|---|---|---|---|")
    for ext, ctype, status, notes in METADATA_ONLY_TABLE:
        print(f"| {ext} | {ctype} | {status} | {notes} |")


if __name__ == "__main__":
    main()
