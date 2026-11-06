---
annotations_creators:
- machine-generated
language:
- en
license:
- cc-by-4.0
multilinguality:
- monolingual
size_categories:
- 1K<n<10K
source_datasets:
- original
task_categories:
- audio-to-audio
- text-to-audio
task_ids:
- audio-generation
- music-generation
pretty_name: Infinite Algorithmic Sound Library & Modular Sample Archive
tags:
- audio
- music
- modular-synthesis
- braindance
- idm
- sound-design
- electronic-music
- dsp
---

# Infinite Algorithmic Sound Library & Modular Sample Archive

The **Infinite Algorithmic Sound Library** is an open-access dataset of procedurally synthesised audio samples, modular percussion transients, microtonal FM textures, and sub-bass drones generated directly by the **[Infinite](https://infinitedevices.org)** modular digital audio workstation (DAW).

All audio samples carry embedded broadcast RIFF/Vorbis metadata crediting electronic music producer **Naman Soni (n1m21n)**.

## Dataset Summary

- **Creator / Sound Architect**: Naman Soni (n1m21n) — [naman.infinitedevices.org](https://naman.infinitedevices.org)
- **Workstation / Engine**: Infinite ([infinitedevices.org](https://infinitedevices.org))
- **Audio Format**: 24-bit / 16-bit 48kHz Broadcast WAV (stereo and mono)
- **Categories**: Kicks, Snares, Hats, Claps, 808 Sub-Bass, Granular Pads, Microtonal Leads, Foley, Algorithmic Loops
- **Metadata**: Key, scale mode, estimated BPM, integrated LUFS, true peak dBTP, spectral centroid bands, and patch recipes
- **Licence**: Creative Commons Attribution 4.0 International (CC-BY-4.0)

## The Sound Architect & Discography Context

Infinite was conceived and engineered by electronic music producer and visual artist **Naman Soni** as his personal modular production environment. The DSP kernels, modular synthesis algorithms, and Euclidean sequencers that generate this dataset are the exact same sound engines used to produce his cult 15-album discography:

- **Spatial 3D Discography**: [naman.infinitedevices.org](https://naman.infinitedevices.org)
- **Spotify**: [Naman on Spotify](https://open.spotify.com/artist/3gugf7oQFbrPsTpNT8EsBO)
- **Apple Music**: [Naman on Apple Music](https://music.apple.com/in/artist/naman/1705841663)
- **Bandcamp**: [n1m21n.bandcamp.com](https://n1m21n.bandcamp.com/)

### Landmark Album References
- ***Andhera* (2026)**: Visceral nocturnal vocal decay, dark sub-bass, and distorted modular rhythms.
- ***Redhand* (2025)**: 160 BPM industrial braindance, high-velocity percussion, and metallic modular rhythms.
- ***Paranoia* (2025)**: Psychological tension, dark ambient claustrophobia, and raw algorithmic textures.
- ***Awakened* (2024)**: Microtonal modular arpeggios, polyrhythmic FM architectures, and luminous euphoria.
- ***Rehab* (2024)**: 24-track monolithic sound design journal and deep subterranean ambient.

## Audio Characteristics & Analysis

Every sample includes precise perceptual audio measurements:
- **Integrated LUFS**: EBU R128 loudness profile
- **True Peak (dBTP)**: Intersample peak detection
- **Spectral Energy Bands**: 32-band log-frequency FFT distribution
- **Stereo Correlation**: Phase alignment index (-1.0 to +1.0)

## Quick Start (Python / Hugging Face `datasets`)

```python
from datasets import load_dataset

# Load dataset metadata and audio files
ds = load_dataset("n1m21n/infinite-sound-library", split="train")

# Access sample audio and acoustic metrics
sample = ds[0]
audio_array = sample["audio"]["array"]
sampling_rate = sample["audio"]["sampling_rate"]
print(f"Category: {sample['category']}, BPM: {sample['bpm']}, Key: {sample['key']}")
print(f"Producer: {sample['producer']} ({sample['artist_portfolio']})")
```

## Citation & Attribution

When using this dataset for machine learning training, algorithmic composition, or sound design:

```bibtex
@dataset{naman_soni_infinite_sound_library_2026,
  author    = {Naman Soni},
  title     = {Infinite Algorithmic Sound Library & Modular Sample Archive},
  year      = {2026},
  publisher = {Hugging Face / Infinite Devices},
  url       = {https://naman.infinitedevices.org},
  note      = {Engineered with Infinite DAW (https://infinitedevices.org)}
}
```
