"""
weights.py
The ranking weights the nightly sleep job (l1/sleep.py) may retune. Defaults are the last
values the replay kept; l1/state/learned_weights.json (local) overrides them once sleep has
found a set that scores better on held-out prompt cases.

  session_w  RRF weight of the files edited earlier in the same session   (l3/recent.py)
  recent_w   RRF weight of the files edited lately anywhere               (l3/recent.py)
  tau_days   decay of "lately": each edit counts exp(-age / tau_days)
"""

import json
from pathlib import Path

DEFAULTS = {"session_w": 1.0, "recent_w": 0.25, "tau_days": 2.0}
LEARNED = Path(__file__).resolve().parents[1] / "l1" / "state" / "learned_weights.json"


def load(path=LEARNED):
    out = dict(DEFAULTS)
    try:
        learned = json.loads(Path(path).read_text()).get("weights", {})
    except (OSError, ValueError):
        learned = {}
    for k, v in learned.items():
        if k in out and isinstance(v, (int, float)) and v >= 0:
            out[k] = float(v)
    return out
