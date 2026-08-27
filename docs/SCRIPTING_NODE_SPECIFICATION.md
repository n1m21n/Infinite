# Scripting Node — Architecture & Technical Specification

> **System Target**: Audiovisual Modular Node Graph Engine (Infinite / Bespoke DAG Architecture)  
> **Author / Project**: Technical Architecture Document  
> **Date**: August 2026  
> **Status**: Proposed / Specification Draft  

---

## 1. Executive Summary & Objective

The **Scripting Node** is a dynamic, user-programmable node that allows live execution of native scripting languages directly inside the audiovisual DAG (Directed Acyclic Graph). 

### Core Capabilities
- **Triple Output Stream Generation**: Outputs synchronized **Video** (GPU texture), **Audio** (stereo/multichannel float buffers), and **Notes** (sample-accurate MIDI / `NoteEvent` streams).
- **Dynamic Parameter Reflection**: Automatically detects user-declared variables in code (`# @param`), generates standard UI sliders/toggles/menus, and exposes CV modulation pins on the node canvas.
- **Strict Compliance with Software Standards**: Follows Infinite's strict two-DAG split, `ParamMailbox` thread handoff, `INoteSource` dispatch, and self-contained zero-install deployment.
- **Zero-Dropout Hot Reloading**: Live recompilation upon file save or inline editing, preserving internal state across executions.

---

## 2. High-Level System Architecture & Node Layout

```
                                ┌──────────────────────────────────────────────────────────┐
                                │                     SCRIPTING NODE                       │
                                │                                                          │
   [CV Modulation Pins] ───────►│  ┌────────────────────────────────────────────────────┐  │
   (auto-generated per param)   │  │ Dynamic Param Sliders (Double-click reset, CV depths)│
                                │  └────────────────────────────────────────────────────┘  │
                                │                                                          │
   [Video In (Optional)] ──────►│  ┌────────────────────────────────────────────────────┐  │────────► [ Video Output ]
   (OpenGL PBO / Metal)         │  │ Script Engine (Embedded Python / LuaJIT)           │  │          (GPU Texture Pin)
                                │  │   process_video()                                  │  │
   [Audio In (Optional)] ──────►│  │   process_audio()                                  │  │────────► [ Audio Output ]
   (Float32 Stereo Buffer)      │  │   process_notes() / node.play_note()               │  │          (Audio Pin)
                                │  └────────────────────────────────────────────────────┘  │
   [Note In (Optional)] ───────►│                                                          │────────► [ Note Output ]
   (NoteCable / INoteSource)    │  [Status: RUNNING 🟢 | 60.0 FPS | Audio DSP: 0.6ms]       │          (NoteCable Pin)
                                └──────────────────────────────────────────────────────────┘
```

---

## 3. Dependency & Installation Model ("Does it require external installs?")

A fundamental pillar of Infinite is that it builds as a **self-contained binary requiring zero external package managers or user terminal setup**.

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│ ZERO-INSTALL OUT-OF-THE-BOX ARCHITECTURE                                                │
│                                                                                         │
│  1. Bundled Embedded Runtime (Shipped with App Binary):                                 │
│     • macOS: Embedded Python.framework (or LuaJIT) inside Infinite.app/Contents/        │
│     • Windows: Bundled python311.dll + standard library zip in app directory.           │
│     • Pre-bundled Core Extensions: NumPy, Math, Audio DSP, Random, Matrix Math.         │
│     ➔ RESULT: Works immediately on a fresh machine with ZERO user setup.                │
│                                                                                         │
│  2. Optional "Power User" Virtual Environment Link (BYOP):                              │
│     • Users wishing to import torch, opencv-python, or librosa can point the            │
│       Node / App Preferences to their existing Python venv path.                        │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Script API Contract (Video, Audio & Notes)

The script API implements clean hooks for visual frames, audio DSP, and note event dispatching.

```python
import numpy as np
import math

# ==============================================================================
# 1. DYNAMIC PARAMETERS & CV EXPOSURE
# Follows standard UI formatting: double-click to reset, right-click to map CV.
# ==============================================================================
bpm_sync     = 120.0   # @param float: min=40.0, max=240.0, step=1.0, label="Tempo (BPM)"
root_note    = 60      # @param int: min=0, max=127, label="Root Pitch (MIDI)"
scale_mode   = "Minor" # @param choice: ["Minor", "Major", "Dorian", "Pentatonic"], label="Scale"
color_shift  = 0.5     # @param float: min=0.0, max=1.0, label="Visual Glow"
gate_length  = 0.25    # @param float: min=0.05, max=1.0, label="Gate Length"

# Persistent State
_phase = 0.0
_step_counter = 0

# ==============================================================================
# 2. LIFECYCLE & PROCESSING HOOKS
# ==============================================================================

def setup(node):
    """Called once on initialization or script reload."""
    node.log("Scripting node ready. Video, Audio, and Note engines active.")

def process_video(node, frame_in=None):
    """
    Renders 1 frame to the GPU texture output (60 FPS).
    Returns: uint8 (H, W, 4) RGBA array or GPU texture ID.
    """
    w, h = node.width, node.height
    t = node.time
    
    y, x = np.mgrid[0:h, 0:w]
    r = np.sin(x * 0.02 + t * 2.0 + color_shift * 4.0)
    g = np.cos(y * 0.02 + t * 1.5)
    b = np.sin((x + y) * 0.01 + t * 2.5)
    
    frame = (np.stack([r, g, b, np.ones_like(r)], axis=-1) * 127.5 + 127.5).astype(np.uint8)
    return frame

def process_audio(node, block_size, sample_rate, audio_in=None):
    """
    Processes or synthesizes an audio block (e.g. 256 samples @ 48kHz).
    Returns: float32 (block_size, 2) stereo array in range [-1.0, 1.0].
    """
    global _phase
    freq = 440.0 * (2.0 ** ((root_note - 69) / 12.0))
    t = (np.arange(block_size) + _phase) / sample_rate
    _phase = (_phase + block_size) % sample_rate
    
    left = 0.5 * np.sin(2.0 * np.pi * freq * t)
    right = 0.5 * np.sin(2.0 * np.pi * (freq * 1.002) * t)
    return np.stack([left, right], axis=-1).astype(np.float32)

def process_notes(node, block_size, sample_rate, note_events_in=[]):
    """
    Dispatches or processes note events on the NoteCable.
    Methods available:
      • node.play_note(pitch, velocity, duration_seconds, frame_offset=0)
      • node.send_midi(status, data1, data2, frame_offset=0)
    """
    global _step_counter
    
    # Example: Step sequencer / generative arpeggiator
    samples_per_beat = int(sample_rate * (60.0 / bpm_sync) * 0.25)
    _step_counter += block_size
    
    if _step_counter >= samples_per_beat:
        _step_counter = 0
        chord_offsets = [0, 3, 7, 10] if scale_mode == "Minor" else [0, 4, 7, 11]
        pitch = root_note + np.random.choice(chord_offsets)
        
        # Fire note out to downstream synths / MIDI outputs
        node.play_note(pitch=pitch, velocity=100, duration_seconds=gate_length)
```

---

## 5. UI/UX Standards Alignment (Infinite Workstation Design)

The Scripting Node UI adheres directly to the rules in `docs/CODE_STANDARDS.md`:

1. **Standard Node Canvas Anatomy**:
   - **Header**: Title, Node Type Badge (`SCRIPT`), Status Dot (🟢/🟡/🔴), Bypass and Mute toggles.
   - **Live Thumbnail**: 1:1 Live preview thumbnail showing live video frames, audio waveforms, or note trigger pulses.
   - **Dynamic Parameters**: Rendered as native Infinite sliders with modulation dots, double-click default reset, and numeric drag.
   - **Bottom Tray**: Collapsible code drawer with tabs: `[Code Editor]`, `[File Watcher]`, `[Console / Logs]`.
2. **Patch Cable Connections**:
   - **Image Pin (Blue)**: Connects to Image / Texture DAG.
   - **Audio Pin (Orange/White)**: Connects to Audio DSP DAG.
   - **Note Pin (Yellow/Cyan)**: Connects to Note Sequencers, Wavetable synths, AU/VST3 hosted plugins (`INoteSource`).
   - **Modulation / CV Pins**: Connects to LFOs, Envelopes, and Math modulators.
3. **Patch Serialization & Save/Load (`ParamVisitor`)**:
   - Serializes script text (or external file path) and all dynamic parameter values cleanly into the `.infinite` patch file format.

---

## 6. Architecture & Concurrency Rules

In compliance with `docs/CODE_STANDARDS.md` Section 3 (*"Respect the two-DAG split & cross-thread ownership"*):

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│ 1. REAL-TIME AUDIO & NOTE THREAD (Priority 99 - No Allocations - No GIL)                │
│    • Consumes audio from SPSC Lock-Free RingBuffer.                                     │
│    • Dispatches NoteEvents through AudioNode's NoteOutbox directly to note cables.      │
│    • Latency: Sample-accurate ($<3\text{ms}$).                                          │
└───────────────────────────────────────────▲─────────────────────────────────────────────┘
                                            │
                                 Lock-Free RingBuffer + NoteQueue
                                            │
┌───────────────────────────────────────────┴─────────────────────────────────────────────┐
│ 2. SCRIPT WORKER THREAD (Background Worker)                                             │
│    • Evaluates script, runs process_audio() & process_notes() ahead into ringbuffer.    │
│    • Dispatches GPU texture rendering into Double-Buffered OpenGL PBO.                  │
│    • Receives parameter updates via ParamMailbox.                                       │
└───────────────────────────────────────────┬─────────────────────────────────────────────┘
                                            │
                                 Double-Buffered GPU PBO
                                            │
┌───────────────────────────────────────────▼─────────────────────────────────────────────┐
│ 3. MAIN / UI & GPU RENDER THREAD                                                        │
│    • Draws node canvas, code editor, live 1:1 preview thumbnail, and status badges.     │
│    • Cooks visual DAG texture without stalling UI.                                      │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```
