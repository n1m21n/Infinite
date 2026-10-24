#!/usr/bin/env python3
"""Infinite Interactive Embed Generator: Turns patches into embeddable web widgets.

What: Generates a self-contained, responsive HTML5 WebGL + WebAudio interactive widget
      from any Field Pixel shader or preset. The widget includes real-time audio reactivity
      (via microphone or procedural synth audio), playback controls, and a persistent,
      clickable "Made with Infinite" watermark linking to https://infinitedevices.org.
Why:  Allowing creators, bloggers, and artists to embed interactive audio-reactive visuals
      on their personal websites, portfolios, Substack, and GitHub pages turns every
      embedded patch into a permanent traffic generator for infinitedevices.org.

Usage:
    python3 tools/spread/embed_gen.py --field website/assets/library/pixel/iridescent-orb.field --out dist/embeds/iridescent-orb.html
"""

import argparse
import html
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

HTML_TEMPLATE = """<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>{TITLE} — Interactive Infinite Visualizer</title>
  <style>
    * {{ box-sizing: border-box; margin: 0; padding: 0; }}
    body, html {{ width: 100%; height: 100%; overflow: hidden; background: #0b0d17; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }}
    canvas {{ width: 100%; height: 100%; display: block; }}
    .badge {{
      position: absolute;
      bottom: 14px;
      right: 14px;
      background: rgba(15, 18, 35, 0.82);
      backdrop-filter: blur(10px);
      -webkit-backdrop-filter: blur(10px);
      border: 1px solid rgba(255, 255, 255, 0.15);
      padding: 6px 12px;
      border-radius: 999px;
      color: #fff;
      font-size: 11px;
      font-weight: 500;
      text-decoration: none;
      display: flex;
      align-items: center;
      gap: 6px;
      box-shadow: 0 4px 12px rgba(0,0,0,0.3);
      transition: transform 0.2s, background 0.2s;
      z-index: 10;
    }}
    .badge:hover {{
      transform: translateY(-2px);
      background: rgba(30, 35, 65, 0.95);
      border-color: rgba(245, 127, 102, 0.5);
    }}
    .badge-icon {{
      width: 14px;
      height: 14px;
      border-radius: 50%;
      background: linear-gradient(135deg, #F57F66, #A9CDF1);
      display: inline-block;
    }}
    .controls {{
      position: absolute;
      bottom: 14px;
      left: 14px;
      display: flex;
      gap: 8px;
      z-index: 10;
    }}
    .btn {{
      background: rgba(15, 18, 35, 0.82);
      backdrop-filter: blur(8px);
      border: 1px solid rgba(255, 255, 255, 0.15);
      color: #eaeaea;
      padding: 6px 10px;
      border-radius: 6px;
      font-size: 11px;
      cursor: pointer;
    }}
    .btn:hover {{ background: rgba(255, 255, 255, 0.15); }}
  </style>
</head>
<body>
  <canvas id="gl"></canvas>

  <div class="controls">
    <button class="btn" id="btn-audio">enable mic input</button>
  </div>

  <a href="https://infinitedevices.org" target="_blank" rel="noopener" class="badge" title="infinite: the open-source audiovisual daw">
    <span class="badge-icon"></span>
    <span>made in <strong>infinite</strong></span>
  </a>

  <script>
    const canvas = document.getElementById("gl");
    const gl = canvas.getContext("webgl");
    if (!gl) {{ document.body.innerHTML = "webgl not supported"; }}

    const vsSource = `
      attribute vec2 position;
      void main() {{
        gl_Position = vec4(position, 0.0, 1.0);
      }}
    `;

    // Standard fragment shader wrapper converting Field logic to WebGL
    const fsSource = `
      precision highp float;
      uniform vec2 res;
      uniform float t;
      uniform float aspect;
      uniform float audio_bass;
      uniform float audio_mid;
      uniform float audio_high;

      float atan2(float y, float x) {{
        return atan(y, x);
      }}

      void main() {{
        vec2 uv = gl_FragCoord.xy / res.xy;
        vec2 p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);
        vec3 col = vec3(0.0);
        float alpha = 1.0;

        {SHADER_BODY}

        gl_FragColor = vec4(col, alpha);
      }}
    `;

    function createShader(gl, type, source) {{
      const shader = gl.createShader(type);
      gl.shaderSource(shader, source);
      gl.compileShader(shader);
      if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {{
        console.error(gl.getShaderInfoLog(shader));
        gl.deleteShader(shader);
        return null;
      }}
      return shader;
    }}

    const program = gl.createProgram();
    gl.attachShader(program, createShader(gl, gl.VERTEX_SHADER, vsSource));
    gl.attachShader(program, createShader(gl, gl.FRAGMENT_SHADER, fsSource));
    gl.linkProgram(program);

    const positionBuffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
      -1, -1,  1, -1, -1,  1,
      -1,  1,  1, -1,  1,  1
    ]), gl.STATIC_DRAW);

    const posLoc = gl.getAttribLocation(program, "position");
    gl.enableVertexAttribArray(posLoc);
    gl.vertexAttribPointer(posLoc, 2, gl.FLOAT, false, 0, 0);

    const uRes = gl.getUniformLocation(program, "res");
    const uTime = gl.getUniformLocation(program, "t");
    const uAspect = gl.getUniformLocation(program, "aspect");
    const uBass = gl.getUniformLocation(program, "audio_bass");
    const uMid = gl.getUniformLocation(program, "audio_mid");
    const uHigh = gl.getUniformLocation(program, "audio_high");

    let audioContext = null;
    let analyser = null;
    let freqData = null;
    let bass = 0.5, mid = 0.5, high = 0.5;

    document.getElementById("btn-audio").addEventListener("click", async () => {{
      if (!audioContext) {{
        audioContext = new (window.AudioContext || window.webkitAudioContext)();
        try {{
          const stream = await navigator.mediaDevices.getUserMedia({{ audio: true }});
          const source = audioContext.createMediaStreamSource(stream);
          analyser = audioContext.createAnalyser();
          analyser.fftSize = 64;
          source.connect(analyser);
          freqData = new Uint8Array(analyser.frequencyBinCount);
          document.getElementById("btn-audio").innerText = "mic active";
        }} catch(e) {{
          document.getElementById("btn-audio").innerText = "mic denied";
        }}
      }}
    }});

    function resize() {{
      const dpr = Math.min(window.devicePixelRatio || 1, 2);
      if (canvas.width !== window.innerWidth * dpr || canvas.height !== window.innerHeight * dpr) {{
        canvas.width = window.innerWidth * dpr;
        canvas.height = window.innerHeight * dpr;
        gl.viewport(0, 0, canvas.width, canvas.height);
      }}
    }}

    const start = performance.now();
    function render() {{
      resize();
      gl.useProgram(program);

      const elapsed = (performance.now() - start) * 0.001;

      if (analyser && freqData) {{
        analyser.getByteFrequencyData(freqData);
        bass = (freqData[1] + freqData[2]) / 255.0;
        mid = (freqData[8] + freqData[9]) / 255.0;
        high = (freqData[20] + freqData[21]) / 255.0;
      }} else {{
        bass = 0.5 + 0.3 * Math.sin(elapsed * 2.0);
        mid = 0.5 + 0.3 * Math.cos(elapsed * 3.1);
        high = 0.5 + 0.3 * Math.sin(elapsed * 5.0);
      }}

      gl.uniform2f(uRes, canvas.width, canvas.height);
      gl.uniform1f(uTime, elapsed);
      gl.uniform1f(uAspect, canvas.width / canvas.height);
      gl.uniform1f(uBass, bass);
      gl.uniform1f(uMid, mid);
      gl.uniform1f(uHigh, high);

      gl.drawArrays(gl.TRIANGLES, 0, 6);
      requestAnimationFrame(render);
    }}
    render();
  </script>
</body>
</html>
"""


def main():
    parser = argparse.ArgumentParser(description="Infinite Interactive Web Embed Generator")
    parser.add_argument("--field", help="Path to .field file")
    parser.add_argument("--glsl", help="Path to raw GLSL file")
    parser.add_argument("--style", help="Art taxonomy style id (from art_taxonomy.py)")
    parser.add_argument("--out", help="Destination HTML file")
    parser.add_argument("--all", action="store_true", help="Generate embeds for all field devices in website/assets/library/pixel")
    parser.add_argument("--out-dir", default="website/embeds", help="Output directory for --all")
    parser.add_argument("--title", default="Audio-Reactive Patch", help="Widget title")

    args = parser.parse_args()

    if args.all:
        import glob
        pixel_dir = os.path.join(ROOT, "website", "assets", "library", "pixel")
        out_dir = os.path.join(ROOT, args.out_dir)
        os.makedirs(out_dir, exist_ok=True)
        fields = sorted(glob.glob(os.path.join(pixel_dir, "*.field")))
        count = 0
        for fpath in fields:
            name = os.path.splitext(os.path.basename(fpath))[0]
            out_file = os.path.join(out_dir, f"{name}.html")
            data = json.load(open(fpath))
            shader_body = data["device"]["code"]
            title = data["meta"].get("name", name)
            cleaned_body = []
            for line in shader_body.splitlines():
                if line.strip().startswith("param "):
                    cleaned = line.split("[")[0].strip().replace("param ", "") + ";"
                    cleaned_body.append(cleaned)
                else:
                    cleaned_body.append(line)
            output_html = HTML_TEMPLATE.format(TITLE=html.escape(title), SHADER_BODY="\n        ".join(cleaned_body))
            with open(out_file, "w") as f:
                f.write(output_html)
            count += 1
        print(f"generated {count} interactive embeds in {args.out_dir}/")
        return

    shader_body = ""
    title = args.title

    if not args.out:
        print("[!] Must provide --out <file> or --all")
        sys.exit(1)

    if args.field:
        data = json.load(open(args.field))
        shader_body = data["device"]["code"]
        title = data["meta"].get("name", title)
    elif args.style:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from art_taxonomy import TAXONOMY
        st = TAXONOMY.get(args.style)
        if not st:
            print(f"[!] Unknown style: {args.style}")
            sys.exit(1)
        shader_body = st.get("field_pixel", "")
        title = st.get("name", args.style)
    elif args.glsl:
        shader_body = open(args.glsl).read()
    else:
        print("[!] Must provide either --field, --style, or --glsl")
        sys.exit(1)

    # Basic parameter translation if from Field
    cleaned_body = []
    for line in shader_body.splitlines():
        if line.strip().startswith("param "):
            cleaned = line.split("[")[0].strip().replace("param ", "") + ";"
            cleaned_body.append(cleaned)
        else:
            cleaned_body.append(line)

    output_html = HTML_TEMPLATE.format(
        TITLE=html.escape(title),
        SHADER_BODY="\n        ".join(cleaned_body)
    )

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w") as f:
        f.write(output_html)

    print(f"generated interactive embed: {args.out}")
    print()
    print("copy-paste embed code for any website or blog:")
    print(f'<iframe src="https://infinitedevices.org/embeds/{os.path.basename(args.out)}" width="640" height="400" frameborder="0" allow="microphone" allowfullscreen></iframe>')


if __name__ == "__main__":
    main()
