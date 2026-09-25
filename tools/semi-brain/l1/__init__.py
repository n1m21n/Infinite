"""L1 of the semi-brain: content-hash doc store, shared vector cache, per-source watermarks,
shared entity table. Everything under l1/state/ is local-only (see tools/semi-brain/.gitignore)."""
from pathlib import Path

L1_DIR = Path(__file__).resolve().parent
STATE_DIR = L1_DIR / "state"
SEMI_BRAIN_DIR = L1_DIR.parent
REPO_PATH = SEMI_BRAIN_DIR.parents[1]
EMBED_MODEL = "BAAI/bge-small-en-v1.5"
