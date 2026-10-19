# Infinite Autonomous Growth & Distribution Engine (`tools/spread/`)

This toolkit contains creative, programmatic mechanisms designed to spread **Infinite** ([infinitedevices.org](https://infinitedevices.org)) across the internet without reliance on ad spend or generic promotional posts.

> **Rules that bind every tool here.** Nothing is posted automatically. The sentinel and radar only find threads and prepare drafts; a person writes and sends each reply, one thread at a time, mentioning Infinite only where it fits and naming one honest limit. Articles are written in `tools/seo/posts/`, linted, and go live on infinitedevices.org first; syndication creates drafts, and going live on another site needs Naman's yes per post. Caps, tone and account rules: `tools/seo/RULES.md`. The shader porter's tagging template below is a starting point for a hand-written message, never to be sent as is. `syndicate.py` is deprecated in favour of `tools/seo/publish.py`.
>
> **Finding the app.** The SDK, MCP server and these tools look for the Infinite executable in `$INFINITE_BIN`, the checkout's `build` folders, then `PATH`. On Windows and Linux set `INFINITE_BIN` to the installed `Infinite.exe` / `Infinite`.

---

## 1. 📡 Intent Radar (`tools/spread/radar.py`)
**Concept**: Target high-intent queries where creators are actively looking for solutions that only Infinite provides.

### How it works:
Scans Hacker News (Algolia API) and GitHub Discussions/Issues for active threads asking about:
- *TouchDesigner free/open-source alternatives*
- *Modular VST3 hosts with CV/LFO modulation*
- *Real-time audio-reactive GLSL shaders*
- *Linux modular DAWs and audiovisual performance workstations*

### Commands:
```bash
# Scan all channels for high-intent questions
python3 tools/spread/radar.py --scan

# Scan for a custom query
python3 tools/spread/radar.py --query "TouchDesigner alternative"

# Generate an authentic, value-first response with node recipes
python3 tools/spread/radar.py --answer "https://news.ycombinator.com/item?id=..."
```

---

## 2. 🎨 Shader-to-Device Auto-Porter & Creator Attribution Loop (`tools/spread/shader_porter.py`)
**Concept**: Turn prominent creative coders into your biggest ambassadors by converting their static visual shaders into playable audio-reactive synthesizer instruments.

### How it works:
1. Adapts GLSL/Shadertoy code (`fragCoord`, `iTime`, `iResolution`) into Infinite's `Field Pixel` kernel.
2. Automatically injects audio-reactive uniforms (`audio_bass`, `audio_mid`, `audio_high`, `reactivity`).
3. Packages a valid `.field` device file for Infinite's library.
4. Generates an outreach/tagging template for Twitter/Bluesky:
   > *"Hey @artist! Loved your shader '[Name]'. I turned it into a playable, audio-reactive synth device in Infinite (open-source DAW). Check it out here: infinitedevices.org"*

### Commands:
```bash
python3 tools/spread/shader_porter.py \
  --name "Neon Wave" \
  --author "InigoQuilez" \
  --input my_shader.glsl \
  --out website/assets/library/pixel/neon-wave.field
```

---

## 3. 🌐 Interactive Web Embed Generator (`tools/spread/embed_gen.py`)
**Concept**: The "CodePen/Spotify Embed" virality mechanic.

### How it works:
Turns any `.field` shader or patch into a standalone, interactive HTML5 WebGL + WebAudio widget with microphone reactivity and a prominent, elegant **"Made in Infinite"** badge linking back to `infinitedevices.org`.

### Commands:
```bash
python3 tools/spread/embed_gen.py \
  --field website/assets/library/pixel/iridescent-orb.field \
  --out dist/embeds/iridescent-orb.html
```
Anyone can embed this directly into their blogs, portfolios, or documentation:
```html
<iframe src="https://infinitedevices.org/embeds/iridescent-orb.html" width="640" height="400" frameborder="0" allow="microphone"></iframe>
```

---

## 4. 📺 24/7 Generative Stream Bot (`tools/spread/stream_bot.py`)
**Concept**: Permanent ambient discovery billboard.

### How it works:
Generative music & visual streams on YouTube Live and Twitch run continuously, attracting thousands of coders, designers, and modular synth fans looking for focus/ambient feeds.
- Uses Infinite's headless renderer to render seamless loops.
- Pipes audio + video to RTMP with an on-screen HUD:
  `⚡ Infinite: Free Audiovisual DAW • infinitedevices.org`

### Commands:
```bash
# Prepare loops
python3 tools/spread/stream_bot.py --prep-loops

# Output FFmpeg 24/7 stream command
python3 tools/spread/stream_bot.py --stream --rtmp "rtmp://a.rtmp.youtube.com/live2/YOUR_KEY"
```

---

## 5. 🏷️ Dynamic GitHub Profile Visualizer Badge (`tools/spread/badge_gen.py`)
**Concept**: Virality through developer profile customization.

### How it works:
Generates dynamic SVG visualizers that animate natively inside GitHub README markdown files. Every developer who uses it becomes a permanent link back to `infinitedevices.org`.

### Commands:
```bash
python3 tools/spread/badge_gen.py --out website/assets/badges/visualizer.svg
```
Embed snippet:
```markdown
[![Infinite Visualizer](https://infinitedevices.org/assets/badges/visualizer.svg)](https://infinitedevices.org)
```
