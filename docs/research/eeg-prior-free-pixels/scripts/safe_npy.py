"""Load THINGS-EEG2 .npy files that hold pickled dicts, without executing arbitrary pickle code.

The release stores dicts via np.save(allow_pickle=True). np.load(allow_pickle=True) would run any
callable the pickle names, so this loader only allows the handful of classes a dict of numpy arrays,
lists and strings needs. Anything else raises.
"""
import io
import pickle
import zipfile

import numpy as np

_ALLOWED = {
    ("numpy.core.multiarray", "_reconstruct"),
    ("numpy._core.multiarray", "_reconstruct"),
    ("numpy.core.multiarray", "scalar"),
    ("numpy._core.multiarray", "scalar"),
    ("numpy", "ndarray"),
    ("numpy", "dtype"),
    ("builtins", "dict"),
    ("builtins", "list"),
    ("collections", "OrderedDict"),
}


class _Restricted(pickle.Unpickler):
    def find_class(self, module, name):
        if (module, name) in _ALLOWED:
            return super().find_class(module, name)
        raise pickle.UnpicklingError(f"blocked class {module}.{name}")


def load_npy(fp):
    """fp: binary file object positioned at the start of a .npy file."""
    version = np.lib.format.read_magic(fp)
    if version == (1, 0):
        shape, fortran, dtype = np.lib.format.read_array_header_1_0(fp)
    else:
        shape, fortran, dtype = np.lib.format.read_array_header_2_0(fp)
    if dtype.hasobject:
        obj = _Restricted(fp, encoding="latin1").load()
        if isinstance(obj, np.ndarray) and obj.shape == () and obj.dtype == object:
            return obj.item()
        return obj
    data = np.frombuffer(fp.read(), dtype=dtype)
    return data.reshape(shape, order="F" if fortran else "C")


def load_npy_path(path):
    with open(path, "rb") as f:
        return load_npy(f)


def load_npy_from_zip(zip_path, member_suffix):
    with zipfile.ZipFile(zip_path) as z:
        names = [n for n in z.namelist() if n.endswith(member_suffix)]
        if len(names) != 1:
            raise FileNotFoundError(f"{member_suffix}: {names}")
        with z.open(names[0]) as f:
            return load_npy(io.BufferedReader(f, buffer_size=1 << 24))
