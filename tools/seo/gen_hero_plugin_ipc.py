#!/usr/bin/env python3
"""Generates the 1200x675 technical hero diagram for out-of-process-vst3-au-plugin-shared-memory-ipc.
Adheres strictly to docs/brand/BRAND.md and brand.json (Midnight deep #0D1020 mode).
"""
import os
from PIL import Image, ImageDraw, ImageFont

W, H = 1200, 675
img = Image.new("RGBA", (W, H), (13, 16, 32, 255))  # Midnight deep #0D1020
draw = ImageDraw.Draw(img)

# Color tokens from brand.json (Midnight mode)
DEEP = (13, 16, 32, 255)          # #0D1020
GROUND = (21, 25, 48, 255)        # #151930
SURF1 = (32, 38, 63, 255)         # #20263F
SURF2 = (42, 48, 82, 255)         # #2A3052
SURF3 = (52, 59, 94, 255)         # #343B5E
DOT = (35, 42, 74, 255)           # #232A4A
INK = (238, 241, 250, 255)        # #EEF1FA
INK2 = (163, 170, 203, 255)       # #A3AACB
INK3 = (136, 144, 177, 255)       # #8890B1
SIGNAL = (167, 177, 255, 255)     # #A7B1FF
EMBER = (255, 107, 53, 255)       # #FF6B35
GREEN = (74, 222, 128, 255)       # #4ADE80
CYAN = (56, 189, 248, 255)        # #38BDF8

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FONT_DIR = os.path.join(REPO_ROOT, "art", "brand", "fonts")
GEIST_SEMIBOLD = os.path.join(FONT_DIR, "Geist-SemiBold.ttf")
GEIST_MEDIUM = os.path.join(FONT_DIR, "Geist-Medium.ttf")
GEIST_MONO = os.path.join(FONT_DIR, "GeistMono-Medium.ttf")

f_title = ImageFont.truetype(GEIST_SEMIBOLD, 25)
f_sub = ImageFont.truetype(GEIST_MEDIUM, 13)
f_badge = ImageFont.truetype(GEIST_MONO, 11)
f_card_hdr = ImageFont.truetype(GEIST_SEMIBOLD, 13)
f_node_title = ImageFont.truetype(GEIST_SEMIBOLD, 12)
f_body = ImageFont.truetype(GEIST_MEDIUM, 11)
f_mono = ImageFont.truetype(GEIST_MONO, 11)
f_mono_sm = ImageFont.truetype(GEIST_MONO, 10)


def rrect(box, r, fill, outline=None, width=1):
    draw.rounded_rectangle(box, radius=r, fill=fill, outline=outline, width=width)


def arrow_h(x1, x2, y, color, label=None, label_dy=-14, width=2):
    draw.line([(x1, y), (x2, y)], fill=color, width=width)
    d = 1 if x2 > x1 else -1
    draw.polygon([(x2, y), (x2 - d * 8, y - 5), (x2 - d * 8, y + 5)], fill=color)
    if label:
        bbox = draw.textbbox((0, 0), label, font=f_mono_sm)
        lw = bbox[2] - bbox[0]
        draw.text(((x1 + x2 - lw) // 2, y + label_dy), label, font=f_mono_sm, fill=color)


# Subtle dot grid background
for gx in range(24, W, 24):
    for gy in range(24, H, 24):
        draw.ellipse((gx - 1, gy - 1, gx + 1, gy + 1), fill=DOT)

# Header
draw.text((48, 34), "Out-of-process VST3 and AU plugin architecture", font=f_title, fill=INK)
draw.text((48, 68), "Lock-free shared-memory audio rings and triple-buffered PBO pixel transport isolate the DAW host from GPU and DSP stalls", font=f_sub, fill=INK2)

# Top-right badge
rrect((980, 36, 1152, 64), 14, SURF1, outline=SURF3)
draw.text((998, 43), "VST3 · AU · SHM IPC", font=f_badge, fill=SIGNAL)

# Three main columns
# 1. Left: Host DAW Process
L_X0, L_X1 = 48, 356
rrect((L_X0, 112, L_X1, 560), 16, GROUND, outline=SURF3)
draw.text((L_X0 + 20, 130), "HOST DAW PROCESS", font=f_badge, fill=INK3)
draw.text((L_X0 + 20, 148), "Thin native shell (PluginShell)", font=f_card_hdr, fill=INK)

# DAW host sub-cards
rrect((L_X0 + 18, 184, L_X1 - 18, 286), 12, SURF1, outline=SURF2)
draw.text((L_X0 + 32, 198), "ShellSide::Process", font=f_node_title, fill=EMBER)
draw.text((L_X0 + 32, 218), "Posts block [P, P+n) to inRing", font=f_body, fill=INK2)
draw.text((L_X0 + 32, 236), "Reads [P-L, P-L+n) from outRing", font=f_body, fill=INK2)
draw.text((L_X0 + 32, 256), "Zero malloc / zero mutex", font=f_mono_sm, fill=GREEN)

rrect((L_X0 + 18, 304, L_X1 - 18, 406), 12, SURF1, outline=SURF2)
draw.text((L_X0 + 32, 318), "Host Automation & MIDI", font=f_node_title, fill=SIGNAL)
draw.text((L_X0 + 32, 338), "128 host slots (kHostSlots)", font=f_body, fill=INK2)
draw.text((L_X0 + 32, 356), "512 MIDI events / block", font=f_body, fill=INK2)
draw.text((L_X0 + 32, 376), "Above Ableton 64-param limit", font=f_mono_sm, fill=INK3)

rrect((L_X0 + 18, 424, L_X1 - 18, 540), 12, SURF1, outline=SURF2)
draw.text((L_X0 + 32, 438), "Plugin View (120 Hz Timer)", font=f_node_title, fill=CYAN)
draw.text((L_X0 + 32, 458), "Acquires front buffer if kDirty", font=f_body, fill=INK2)
draw.text((L_X0 + 32, 476), "Blits 8-bit BGRA to OS surface:", font=f_body, fill=INK2)
draw.text((L_X0 + 32, 498), "CoreGraphics · BI_RGB · X11", font=f_mono_sm, fill=INK3)
draw.text((L_X0 + 32, 516), "Forwards InputEvent -> engine", font=f_mono_sm, fill=CYAN)

# 2. Centre: Shared Memory IPC
C_X0, C_X1 = 424, 776
rrect((C_X0, 112, C_X1, 560), 16, SURF1, outline=SIGNAL, width=1)
draw.text((C_X0 + 20, 130), "SHARED MEMORY IPC", font=f_badge, fill=SIGNAL)
draw.text((C_X0 + 20, 148), "Zero-copy cross-process regions", font=f_card_hdr, fill=INK)

# PluginIpc region
rrect((C_X0 + 18, 184, C_X1 - 18, 392), 12, DEEP, outline=SURF3)
draw.text((C_X0 + 32, 198), "PluginIpc  (magic 0x494E4650)", font=f_mono, fill=EMBER)
rows = [
    ("Slots [64]", "BlockIn, transport, MIDI, params", SIGNAL),
    ("inRing [4ch]", "Main L/R + sidechain L/R", INK2),
    ("outRing [2ch]", "Cooked stereo output", GREEN),
    ("toEngine", "1 MiB message ring (state, input)", INK3),
    ("toShell", "1 MiB message ring (edits, status)", INK3),
]
ry = 228
for k, desc, col in rows:
    rrect((C_X0 + 30, ry, C_X1 - 30, ry + 26), 6, SURF2)
    draw.text((C_X0 + 38, ry + 6), k, font=f_mono_sm, fill=col)
    draw.text((C_X0 + 126, ry + 6), desc, font=f_mono_sm, fill=INK2)
    ry += 32

# PluginFrames region
rrect((C_X0 + 18, 408, C_X1 - 18, 540), 12, DEEP, outline=SURF3)
draw.text((C_X0 + 32, 422), "PluginFrames  (magic 0x46464E49)", font=f_mono, fill=CYAN)
draw.text((C_X0 + 32, 444), "Lock-free triple buffer (kBuffers = 3)", font=f_body, fill=INK2)
bx = C_X0 + 32
for idx, (lbl, col) in enumerate([("front", CYAN), ("middle | kDirty", EMBER), ("back", GREEN)]):
    rrect((bx, 470, bx + 92, 504), 8, SURF2, outline=col)
    bbox = draw.textbbox((0, 0), lbl, font=f_mono_sm)
    tw = bbox[2] - bbox[0]
    draw.text((bx + (92 - tw) // 2, 480), lbl, font=f_mono_sm, fill=col)
    bx += 100
draw.text((C_X0 + 32, 514), "exchange(middle, ...) · zero wait", font=f_mono_sm, fill=INK3)

# 3. Right: Out-of-process Engine
R_X0, R_X1 = 844, 1152
rrect((R_X0, 112, R_X1, 560), 16, GROUND, outline=SURF3)
draw.text((R_X0 + 20, 130), "OUT-OF-PROCESS ENGINE", font=f_badge, fill=INK3)
draw.text((R_X0 + 20, 148), "Isolated Infinite helper process", font=f_card_hdr, fill=INK)

rrect((R_X0 + 18, 184, R_X1 - 18, 286), 12, SURF1, outline=SURF2)
draw.text((R_X0 + 32, 198), "EngineSide Audio Loop", font=f_node_title, fill=GREEN)
draw.text((R_X0 + 32, 218), "377 nodes · 23 synths · 29 FX", font=f_body, fill=INK2)
draw.text((R_X0 + 32, 236), "Wavetable · Granular · Field", font=f_body, fill=INK2)
draw.text((R_X0 + 32, 256), "Zero-fills gaps on resync", font=f_mono_sm, fill=GREEN)

rrect((R_X0 + 18, 304, R_X1 - 18, 406), 12, SURF1, outline=SURF2)
draw.text((R_X0 + 32, 318), "Hidden GPU Context", font=f_node_title, fill=SIGNAL)
draw.text((R_X0 + 32, 338), "GLSL · Render 3D · Spectrum", font=f_body, fill=INK2)
draw.text((R_X0 + 32, 356), "Isolated from host GL/Metal", font=f_body, fill=INK2)
draw.text((R_X0 + 32, 376), "Optional native pop-out window", font=f_mono_sm, fill=INK3)

rrect((R_X0 + 18, 424, R_X1 - 18, 540), 12, SURF1, outline=SURF2)
draw.text((R_X0 + 32, 438), "Dual PBO Async Readback", font=f_node_title, fill=CYAN)
draw.text((R_X0 + 32, 458), "Maps PBO on next frame swap", font=f_body, fill=INK2)
draw.text((R_X0 + 32, 476), "Zero GPU pipeline stall", font=f_body, fill=INK2)
draw.text((R_X0 + 32, 498), "Stops when ViewState::open == 0", font=f_mono_sm, fill=EMBER)
draw.text((R_X0 + 32, 516), "Heartbeat every 100 ms", font=f_mono_sm, fill=INK3)

# Arrows connecting Left <-> Centre <-> Right
arrow_h(L_X1 + 4, C_X0 - 4, 242, EMBER, "inRing", -15)
arrow_h(C_X0 - 4, L_X1 + 4, 274, GREEN, "outRing", 6)
arrow_h(C_X0 - 4, L_X1 + 4, 486, CYAN, "BGRA", -15)

arrow_h(C_X1 + 4, R_X0 - 4, 242, EMBER, "BlockIn", -15)
arrow_h(R_X0 - 4, C_X1 + 4, 274, GREEN, "audio", 6)
arrow_h(R_X0 - 4, C_X1 + 4, 486, CYAN, "PBO", -15)

# Footer metrics bar
rrect((48, 584, 1152, 642), 12, SURF1, outline=SURF3)
metrics = [
    ("Lookahead latency", "L = maxBlock (5.33 ms @ 256 / 48 kHz)", SIGNAL),
    ("Offline bounce", "Unbounded wait (sample-exact)", GREEN),
    ("Realtime miss", "Bounded wait -> silence + xruns++", EMBER),
    ("Platforms", "macOS VST3/AU · Win/Linux VST3", INK2),
]
mx = 70
for k, v, col in metrics:
    draw.text((mx, 596), k, font=f_mono_sm, fill=INK3)
    draw.text((mx, 614), v, font=f_mono_sm, fill=col)
    mx += 276

out_path = os.path.join(REPO_ROOT, "website", "assets", "blog_out-of-process-vst3-au-plugin-shared-memory-ipc.png")
img.convert("RGB").save(out_path, "PNG", optimize=True)
print("Saved:", out_path)
