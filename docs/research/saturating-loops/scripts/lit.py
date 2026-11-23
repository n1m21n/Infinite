"""Literature set L (README section 6): explicit saturating filters written from published equations.

Each entry: Lure model, declared knob box, which axes are log (cutoff-like), cells for T3, the knob a
largest-safe-range bisection runs on (or None), and whether the model is multi-affine in the knobs
(corners exact for T3, section 8). Zero input throughout; x+ = A x + B phi(C x).
"""
import numpy as np
from satloops import Lure, moog_ABC

def _shift(n):
    return np.eye(n, k=-1)

def moog_fbsat_ABC(g, k):
    """Linear ladder stages, one tanh on the resonance feedback: u1 = tanh(-k y4)."""
    A = np.eye(4) + g * (_shift(4) - np.eye(4))
    B = np.zeros((4, 1)); B[0, 0] = g
    C = np.zeros((1, 4)); C[0, 3] = -k
    return A, B, C

def diode_fbsat_ABC(g, k):
    """Diode ladder (McClellan's continuous matrix at k = 0), forward Euler, tanh on the feedback."""
    Ac0 = np.array([[-2, 1, 0, 0], [1, -2, 1, 0], [0, 1, -2, 1], [0, 0, 1, -1]], float)
    A = np.eye(4) + g * Ac0
    B = np.zeros((4, 1)); B[0, 0] = g
    C = np.zeros((1, 4)); C[0, 3] = -k
    return A, B, C

def tdf2_statesat_ABC(a1, a2):
    """TDF-II biquad, saturated states (after Chowdhury 2020): y = phi(z1), z1+ = phi(z2) - a1 y, z2+ = -a2 y."""
    A = np.zeros((2, 2))
    B = np.array([[-a1, 1.0], [-a2, 0.0]])
    return A, B, np.eye(2)

def tdf2_outsat_ABC(a1, a2):
    """TDF-II biquad, saturated output fed back: z1+ = z2 - a1 phi(z1), z2+ = -a2 phi(z1)."""
    A = np.array([[0.0, 1.0], [0.0, 0.0]])
    B = np.array([[-a1], [-a2]])
    return A, B, np.array([[1.0, 0.0]])

def rossum_2pole_ABC(a1, a2):
    """All-pole 2-pole with the saturator on the feedback sum (Rossum-style): y+ = phi(-a1 y - a2 y_prev)."""
    A = np.array([[0.0, 0.0], [1.0, 0.0]])
    B = np.array([[1.0], [0.0]])
    return A, B, np.array([[-a1, -a2]])

def svf_clip_ABC(f, q):
    """Chamberlin SVF, clipped band-pass integrator: bp+ = clamp(bp + f(-lp - q bp)), lp+ = lp + f bp+."""
    A = np.array([[1.0, 0.0], [0.0, 0.0]])
    B = np.array([[f], [1.0]])
    C = np.array([[-f, 1.0 - f * q]])
    return A, B, C

def onepole_ABC(g, _unused=0.0):
    """Saturating one-pole: y+ = y + g (tanh(in) - tanh(y))."""
    return np.eye(1), np.array([[-g]]), np.eye(1)

def korg35_ABC(g, k):
    """Korg35-style 2-pole (McClellan's Sallen-Key matrix), forward Euler, tanh on the feedback k y2."""
    Ac0 = np.array([[-1.0, 0.0], [1.0, -1.0]])
    A = np.eye(2) + g * Ac0
    B = g * np.array([[-1.0], [1.0]])
    C = np.array([[0.0, k]])
    return A, B, C

def phaser_ABC(a, fb):
    """Four first-order allpasses (y = a u + s, s+ = (1 - a^2) u - a s) in series, clipped feedback
    u1 = clamp(fb d) with d+ = y4 (one sample of delay, so the loop is explicit). Not multi-affine in a."""
    n = 5
    # linear maps from (s, w) to each stage: track u_i as row vectors over [s1..s4, d | w]
    A = np.zeros((n, n)); B = np.zeros((n, 1))
    u_x = np.zeros(n); u_w = 1.0
    for i in range(4):
        e = np.zeros(n); e[i] = 1.0
        y_x = a * u_x + e; y_w = a * u_w
        A[i] = (1 - a * a) * u_x - a * e; B[i, 0] = (1 - a * a) * u_w
        u_x, u_w = y_x, y_w
    A[4] = u_x; B[4, 0] = u_w
    C = np.zeros((1, n)); C[0, 4] = fb
    return A, B, C

def moog_soft(): return Lure("moog-soft", moog_ABC, ["soft"] * 5)

HIRES = ((-1.85, 0.9), (0.0, 0.98))  # (lo, hi) for (a1, a2): a1 in [-1.85, 0], a2 in [0.9, 0.98], inside the stability triangle

L = [
    # name, model, lo, hi, log_axes, cells, range_axis, multi_affine
    ("moog-tanh", Lure("moog-tanh", moog_ABC, ["tanh"] * 5), (1e-4, 0.0), (0.785, 4.0), (0,), [24, 1], 1, True),
    ("moog-soft", moog_soft(), (1e-4, 0.0), (0.785, 4.0), (0,), [24, 1], 1, True),
    ("moog-fbsat", Lure("moog-fbsat", moog_fbsat_ABC, ["tanh"]), (1e-4, 0.0), (0.785, 4.0), (0,), [24, 1], 1, True),
    ("diode-fbsat", Lure("diode-fbsat", diode_fbsat_ABC, ["tanh"]), (1e-4, 0.0), (0.4, 16.0), (0,), [24, 1], 1, True),
    ("korg35-sat", Lure("korg35-sat", korg35_ABC, ["tanh"]), (1e-4, 0.0), (0.5, 2.5), (0,), [24, 1], 1, True),
    ("onepole-tanh", Lure("onepole-tanh", onepole_ABC, ["tanh"]), (1e-4, 0.0), (1.0, 0.0), (0,), [24, 1], None, True),
    ("tdf2-statesat", Lure("tdf2-statesat", tdf2_statesat_ABC, ["tanh", "tanh"]), HIRES[0], HIRES[1], (), [12, 4], None, True),
    ("tdf2-outsat", Lure("tdf2-outsat", tdf2_outsat_ABC, ["tanh"]), HIRES[0], HIRES[1], (), [12, 4], None, True),
    ("rossum-2pole", Lure("rossum-2pole", rossum_2pole_ABC, ["tanh"]), HIRES[0], HIRES[1], (), [12, 4], None, True),
    ("svf-clip", Lure("svf-clip", svf_clip_ABC, ["clamp"]), (0.01, 0.5), (0.5, 1.5), (0,), [12, 4], None, True),
    ("phaser-clip", Lure("phaser-clip", phaser_ABC, ["clamp"]), (-0.9, 0.0), (0.9, 0.95), (), [24, 4], 1, False),
]
