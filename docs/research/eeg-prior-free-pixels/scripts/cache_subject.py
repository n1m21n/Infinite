"""Turn one downloaded sub-XX.zip into compact float32 caches (no pickles), then the zip can be deleted.

Writes CACHE/sub-XX_train.npy  (16540, 17, 100)  training EEG averaged over its 4 repeats
       CACHE/sub-XX_test.npy   (200, 80, 17, 100) every test repeat
       CACHE/sub-XX_info.json  channel names, times, raw shapes
Usage: python cache_subject.py ZIP CACHE_DIR SUB
"""
import json
import sys

import numpy as np

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from safe_npy import load_npy_from_zip  # noqa: E402


def main(zp, cache, sub):
    info = {}
    for split in ("training", "test"):
        d = load_npy_from_zip(zp, f"preprocessed_eeg_{split}.npy")
        x = d["preprocessed_eeg_data"]
        info[f"{split}_shape"] = list(x.shape)
        info[f"{split}_dtype"] = str(x.dtype)
        info["ch_names"] = [str(c) for c in d["ch_names"]]
        info["times"] = [float(t) for t in d["times"]]
        if split == "training":
            np.save(f"{cache}/{sub}_train.npy", x.mean(1).astype(np.float32))
        else:
            np.save(f"{cache}/{sub}_test.npy", x.astype(np.float32))
        del d, x
    with open(f"{cache}/{sub}_info.json", "w") as f:
        json.dump(info, f, indent=1)
    print(sub, info["training_shape"], info["test_shape"], info["training_dtype"])


if __name__ == "__main__":
    main(*sys.argv[1:4])
