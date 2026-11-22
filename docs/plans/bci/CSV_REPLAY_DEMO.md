# Live CSV Replay Demo — Alpha Bell (`bci_alpha_bell.inf`)

Run the **Alpha Bell** biological instrument end-to-end in Infinite using a real **HEROIC Muse 2** EEG recording (no live headset required).

## Ready-to-Use Muse 2 Recording

- **Combined 60 s open → closed loop (recommended):**
  `/Users/namansoni/Infinite/datasets/eeg/heroic_csv/demo_muse2_open_closed.csv`
  *(HEROIC `sub008_remote_session0-202302`, 256 Hz, AF7/AF8/TP9/TP10: 0–30 s Eyes Open → 30–60 s Eyes Closed, 5.1× alpha Berger ratio.)*
- **Separate 61 s files (for full 72 s calibration + held-out scoring):**
  - Open: `/Users/namansoni/Infinite/datasets/eeg/heroic_csv/sub008_remote_session0-202302_open.csv`
  - Closed: `/Users/namansoni/Infinite/datasets/eeg/heroic_csv/sub008_remote_session0-202302_closed.csv`

## 3-Step Demo Instructions

1. **Open the Alpha Bell patch in Infinite:**
   Launch `Infinite.app` and open `assets/examples/bci_alpha_bell.inf` (`File > Open`).
2. **Switch Brain Source to CSV Replay and load the Muse 2 recording:**
   Select the **Brain Source** node (`brain`), open its parameters panel, set **`device`** to **`CSV Replay`**, and paste `/Users/namansoni/Infinite/datasets/eeg/heroic_csv/demo_muse2_open_closed.csv` into **`csv file`** (press Enter). Confirm the status line reads `CSV replay active (256 Hz)` and all four contact pills (`TP9 | AF7 | AF8 | TP10`) turn green.
3. **Watch & listen to the Eyes-Open → Eyes-Closed transition (and optional calibration/blink toggle):**
   - **0–30 s (Eyes Open):** The bell ring stays small and dim, and the audified EEG drone (`brain.audio` → `Reverb` → `Gain`) stays quiet.
   - **30–60 s (Eyes Closed):** As 8–12 Hz alpha surges, `brain.alpha` rises, the bell ring expands and blooms warmly, and the 320 Hz audified alpha tone swells in the reverb.
   - **Optional:** Click **`Calibrate`** on the Brain Source body right at the start of the 60 s loop (30 s Eyes Open → 30 s Eyes Closed) to fit the per-person shrinkage LDA (`alpha` becomes the calibrated eyes-closed score), or set **`blink filter`** to **`Double Blink`** so deliberate double-blinks toggle the live spectrogram overlay (`overlay`) without false triggers from reading/video blinks.
