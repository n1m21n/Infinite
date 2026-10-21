# building a headless daw: synthesizing 48khz modular audio and gpu visuals via cli

> how infinite runs full node graphs headlessly to synthesize 48khz audio, analyze lufs, and render glsl frames without a window server.

![Infinite Main Workstation UI](https://raw.githubusercontent.com/n1m21n/infinite/main/website/assets/screenshot.png)

most digital audio workstations are tightly coupled to their user interfaces. attempting to run ableton live, fl studio, or logic pro from a terminal or continuous integration pipeline either requires complex virtual display wrappers (xvfb) or is outright impossible.

when building [infinite](https://infinitedevices.org), we made the engine fully decoupled from the opengl ui. with the headless cli flag, you can validate patch graphs, synthesize multi-minute 48khz audio files, compute broadcast loudness metrics (integrated lufs, true peak), and render video frames in milliseconds directly from bash.

### 1. why headless audio synthesis matters

offline headless rendering opens up workflows that desktop-only daws cannot touch:
- **automated discord/telegram bots**: community members can type `!inf make fm` or upload a patch file and receive an mp3 export + visual snapshot in seconds.
- **ci/cd regression testing**: verify that dsp algorithms never clip, introduce dc offset, or change timbre across git commits.
- **cloud patch generation**: procedural sound design systems that output `.inf` patches and synthesize stems on demand.

### 2. sample-accurate execution in sub-200ms

because infinite's audio engine uses zero dynamic allocations in the block processing loop, offline rendering runs as fast as cpu cache lines permit:

```bash
Infinite --audio-summary patch.inf summary.json --wav out.wav --duration 4
```

in less than 200ms, the engine instantiates the node graph (oscillators, envelopes, filters, and delays), evaluates 192,000 frames at 48khz, and produces a complete json analysis report:

```json
{
  "ok": true,
  "audio": { "sample_rate": 48000, "frames": 192000 },
  "audio_summary": {
    "integrated_lufs": -9.79,
    "sample_peak_dbfs": -1.65,
    "bpm_estimate": 96.2,
    "clipped_frames": 0
  }
}
```

![Infinite Modular Canvas](https://raw.githubusercontent.com/n1m21n/infinite/main/press_kit/screenshots/01_infinite_main_canvas.png)

### 3. try it out

infinite is free, open source, and available for macos, windows, and linux.

- website & web embeds: https://infinitedevices.org
- github repository: https://github.com/n1m21n/infinite
