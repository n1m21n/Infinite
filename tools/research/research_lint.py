"""Research lint: validate docs/research/<slug>/ projects against docs/RESEARCH_STANDARD.md.

What: checks required structure, frontmatter, pre-registered hypotheses/kill criteria,
threats to validity, ARTIFACT.md matrix, and data/provenance.json schema & outputs.
Why: keeps Lane R research reproducible and prevents unpinned or un-falsifiable claims
from being published.

Usage:
    python3 tools/research/research_lint.py                    # lint all docs/research/<slug>/ (skips _template)
    python3 tools/research/research_lint.py PATH [PATH ...]    # lint specific research project folder(s)
    python3 tools/research/research_lint.py --check-template   # verify docs/research/_template/ integrity

Exit codes:
    0  all checked projects pass (or none exist yet)
    1  one or more lint errors found
    2  usage error
"""
import json
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
RESEARCH_DIR = os.path.join(ROOT, "docs", "research")
TEMPLATE_DIR = os.path.join(RESEARCH_DIR, "_template")

VALID_STATUSES = {
    "phase-0-framing",
    "pre-registered",
    "measuring",
    "validated",
    "killed",
    "published",
}
ADVANCED_STATUSES = {"measuring", "validated", "published"}
SHA40_RE = re.compile(r"^[0-9a-f]{40}$")
PLACEHOLDER_RE = re.compile(
    r"<(?:Research Title|slug|name|YYYY-MM-DD|pinned-sha-when-measured|full-40-char-sha|"
    r"zenodo-doi-or-pending|e\.g\.,[^>]+|compiler[^>]+|global RNG seed[^>]+)>")
PROVENANCE_KEYS = (
    "commit_sha",
    "timestamp_utc",
    "os",
    "cpu",
    "compiler",
    "build_flags",
    "command",
    "seed",
    "outputs",
)


def parse_frontmatter(text):
    """Return (dict, body) if YAML-like frontmatter is present, else (None, text)."""
    if not text.startswith("---\n"):
        return None, text
    end = text.find("\n---\n", 4)
    if end == -1:
        return None, text
    raw = text[4:end]
    meta = {}
    for line in raw.splitlines():
        line = line.strip()
        if not line or line.startswith("#") or ":" not in line:
            continue
        k, v = line.split(":", 1)
        meta[k.strip()] = v.strip().strip('"').strip("'")
    return meta, text[end + 5:]


def check_provenance(project_dir, prov_path, errors):
    """Validate data/provenance.json against provenance.schema.json contract."""
    try:
        with open(prov_path, encoding="utf-8") as f:
            prov = json.load(f)
    except Exception as exc:
        errors.append(f"{prov_path}: invalid JSON ({exc})")
        return

    if not isinstance(prov, dict):
        errors.append(f"{prov_path}: root must be a JSON object")
        return

    for key in PROVENANCE_KEYS:
        if key not in prov:
            errors.append(f"{prov_path}: missing required key '{key}'")

    sha = prov.get("commit_sha", "")
    if not isinstance(sha, str) or not SHA40_RE.match(sha):
        errors.append(f"{prov_path}: 'commit_sha' must be a 40-char lowercase hex SHA")

    seed = prov.get("seed")
    if isinstance(seed, bool) or not isinstance(seed, int):
        errors.append(f"{prov_path}: 'seed' must be an integer")

    for str_key in ("timestamp_utc", "os", "cpu", "compiler", "build_flags", "command"):
        val = prov.get(str_key)
        if not isinstance(val, str) or not val.strip():
            errors.append(f"{prov_path}: '{str_key}' must be a non-empty string")

    outputs = prov.get("outputs")
    if not isinstance(outputs, list) or not outputs:
        errors.append(f"{prov_path}: 'outputs' must be a non-empty list of file paths")
    else:
        for rel in outputs:
            if not isinstance(rel, str) or not rel.strip():
                errors.append(f"{prov_path}: invalid output entry {rel!r}")
                continue
            cand_proj = os.path.join(project_dir, rel)
            cand_data = os.path.join(project_dir, "data", rel)
            if not (os.path.exists(cand_proj) or os.path.exists(cand_data)):
                errors.append(f"{prov_path}: listed output '{rel}' does not exist")


def lint_project(project_dir):
    """Lint one docs/research/<slug>/ directory. Returns list of error strings."""
    errors = []
    rel_dir = os.path.relpath(project_dir, ROOT)
    readme_path = os.path.join(project_dir, "README.md")
    artifact_path = os.path.join(project_dir, "ARTIFACT.md")

    if not os.path.isfile(readme_path):
        return [f"{rel_dir}: missing README.md"]
    if not os.path.isfile(artifact_path):
        errors.append(f"{rel_dir}: missing ARTIFACT.md")

    readme = open(readme_path, encoding="utf-8").read()
    meta, body = parse_frontmatter(readme)
    status = ""

    if meta is None:
        errors.append(f"{rel_dir}/README.md: missing YAML frontmatter (--- ... ---)")
    else:
        for k in ("title", "slug", "status", "owner", "date"):
            if not meta.get(k):
                errors.append(f"{rel_dir}/README.md: frontmatter missing '{k}'")
        status = meta.get("status", "")
        if status and status not in VALID_STATUSES:
            errors.append(
                f"{rel_dir}/README.md: invalid status '{status}' "
                f"(expected one of {sorted(VALID_STATUSES)})"
            )
        if status in ADVANCED_STATUSES:
            sha = meta.get("commit_sha", "")
            if not SHA40_RE.match(sha):
                errors.append(
                    f"{rel_dir}/README.md: status '{status}' requires a 40-char 'commit_sha'"
                )

    for m in PLACEHOLDER_RE.finditer(readme):
        errors.append(f"{rel_dir}/README.md: unfilled placeholder {m.group(0)}")

    headings = "\n".join(
        line.lower() for line in body.splitlines() if line.lstrip().startswith("#")
    )
    if "problem" not in headings:
        errors.append(f"{rel_dir}/README.md: missing Problem section heading")
    if "hypothes" not in headings and "kill" not in headings:
        errors.append(f"{rel_dir}/README.md: missing Hypotheses / Kill Criteria section")
    if "validity" not in headings and "caveat" not in headings:
        errors.append(f"{rel_dir}/README.md: missing Threats to Validity / Caveats section")

    if "seen in search only" in readme.lower():
        errors.append(
            f"{rel_dir}/README.md: contains 'Seen in search only' marker — do not cite unverified sources"
        )

    if os.path.isfile(artifact_path):
        artifact = open(artifact_path, encoding="utf-8").read()
        if "|---" not in artifact and "| ---" not in artifact:
            errors.append(f"{rel_dir}/ARTIFACT.md: missing Claim-to-Artifact Matrix table")
        if status in ADVANCED_STATUSES:
            for m in PLACEHOLDER_RE.finditer(artifact):
                errors.append(f"{rel_dir}/ARTIFACT.md: unfilled placeholder {m.group(0)}")

    data_dir = os.path.join(project_dir, "data")
    prov_path = os.path.join(data_dir, "provenance.json")
    has_data_files = os.path.isdir(data_dir) and any(
        f != "provenance.json" and f.endswith((".csv", ".json"))
        for f in os.listdir(data_dir)
    )
    if has_data_files or status in ADVANCED_STATUSES:
        if not os.path.isfile(prov_path):
            errors.append(f"{rel_dir}/data/provenance.json: missing provenance record")
        else:
            check_provenance(project_dir, prov_path, errors)

    return errors


def check_template():
    """Verify docs/research/_template/ has all expected starter files."""
    required = [
        os.path.join(TEMPLATE_DIR, "README.md"),
        os.path.join(TEMPLATE_DIR, "ARTIFACT.md"),
        os.path.join(TEMPLATE_DIR, "data", "provenance.schema.json"),
    ]
    missing = [os.path.relpath(p, ROOT) for p in required if not os.path.isfile(p)]
    if missing:
        print("Template check failed; missing:", ", ".join(missing), file=sys.stderr)
        return 1
    print("OK: docs/research/_template/ is complete.")
    return 0


def main(argv):
    if "--help" in argv or "-h" in argv:
        print(__doc__.strip())
        return 0
    if argv == ["--check-template"]:
        return check_template()

    targets = []
    if not argv:
        if os.path.isdir(RESEARCH_DIR):
            for entry in sorted(os.listdir(RESEARCH_DIR)):
                if entry.startswith("_") or entry.startswith("."):
                    continue
                full = os.path.join(RESEARCH_DIR, entry)
                if os.path.isdir(full):
                    targets.append(full)
        if not targets:
            print("0 research projects found under docs/research (skipping _template).")
            return 0
    else:
        for arg in argv:
            if arg.startswith("-"):
                print(f"Unknown flag: {arg}", file=sys.stderr)
                return 2
            full = os.path.abspath(arg)
            if not os.path.isdir(full):
                print(f"Not a directory: {arg}", file=sys.stderr)
                return 2
            targets.append(full)

    all_errors = []
    for t in targets:
        all_errors.extend(lint_project(t))

    if all_errors:
        for err in all_errors:
            print(f"FAIL: {err}", file=sys.stderr)
        return 1

    print(f"OK: {len(targets)} research project(s) passed research_lint.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
