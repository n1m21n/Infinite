#!/usr/bin/env python3
"""Infinite Interactive Embed Generator: Turns patches & styles into embeddable web widgets.

Supports:
1. Field Pixel shaders (WebGL fragment)
2. Sketch 2D (HTML5 Canvas + audio reactivity)
3. Sketch 3D (WebGL procedural 3D + audio reactivity)
4. Full Art Styles from art_taxonomy.py
"""

import argparse
import html
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.dirname(__file__))
from art_taxonomy import TAXONOMY

SKETCH_2D_TEMPLATE = """<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>{TITLE} — Infinite 2D Sketch</title>
  <style>
    * {{ box-sizing: border-box; margin: 0; padding: 0; }}
    body, html {{ width: 100%; height: 100%; overflow: hidden; background: #0b0d17; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }}
    canvas {{ width: 100%; height: 100%; display: block; }}
    .badge {{
      position: absolute;
      bottom: 14px;
      right: 14px;
      background: rgba(15, 18, 35, 0.85);
      backdrop-filter: blur(10px);
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
      z-index: 10;
    }}
    .badge-icon {{ width: 12px; height: 12px; border-radius: 50%; background: linear-gradient(135deg, #F57F66, #A9CDF1); }}
    .controls {{ position: absolute; bottom: 14px; left: 14px; display: flex; gap: 8px; z-index: 10; }}
    .btn {{
      background: rgba(15, 18, 35, 0.85);
      backdrop-filter: blur(8px);
      border: 1px solid rgba(255, 255, 255, 0.15);
      color: #eaeaea;
      padding: 6px 10px;
      border-radius: 6px;
      font-size: 11px;
      cursor: pointer;
    }}
    .btn:hover {{ background: rgba(255, 255, 255, 0.2); }}
  </style>
</head>
<body>
  <canvas id="cv"></canvas>
  <div class="controls">
    <button class="btn" id="btn-audio">enable mic input</button>
  </div>
  <a href="https://infinitedevices.org" target="_blank" rel="noopener" class="badge">
    <span class="badge-icon"></span>
    <span>{STYLE_LABEL} • made in <strong>infinite</strong></span>
  </a>

  <script>
    const canvas = document.getElementById("cv");
    const ctx = canvas.getContext("2d");
    let width = 800, height = 600;
    const PI = Math.PI, TAU = Math.PI * 2;
    const sin = Math.sin, cos = Math.cos, min = Math.min, max = Math.max, floor = Math.floor, sqrt = Math.sqrt, abs = Math.abs, dist = Math.hypot;

    function resize() {{
      const dpr = Math.min(window.devicePixelRatio || 1, 2);
      width = canvas.width = window.innerWidth * dpr;
      height = canvas.height = window.innerHeight * dpr;
    }}
    window.addEventListener("resize", resize);
    resize();

    // Infinite Sketch compatibility runtime
    let _fill = "white", _stroke = null, _lineWidth = 1;
    let _shapeActive = false, _shapePts = [];
    function background(...args) {{
      ctx.save();
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      if (args.length === 1 && typeof args[0] === 'number') {{
        const v = floor(args[0] * 255); ctx.fillStyle = `rgb(${v},${v},${v})`;
      }} else if (args.length === 3) {{
        ctx.fillStyle = `rgb(${floor(args[0]*255)},${floor(args[1]*255)},${floor(args[2]*255)})`;
      }} else if (typeof args[0] === 'string') {{ ctx.fillStyle = args[0]; }}
      ctx.fillRect(0, 0, width, height);
      ctx.restore();
    }}
    function fill(...args) {{
      if (typeof args[0] === 'string') {{ _fill = args[0]; }}
      else if (args.length === 1) {{ const v = floor(args[0]*255); _fill = `rgb(${v},${v},${v})`; }}
      else if (args.length >= 3) {{ const a = args[3] !== undefined ? args[3] : 1; _fill = `rgba(${floor(args[0]*255)},${floor(args[1]*255)},${floor(args[2]*255)},${a})`; }}
    }}
    function noFill() {{ _fill = null; }}
    function stroke(...args) {{
      if (typeof args[0] === 'string') {{ _stroke = args[0]; }}
      else if (args.length === 1) {{ const v = floor(args[0]*255); _stroke = `rgb(${v},${v},${v})`; }}
      else if (args.length >= 3) {{ const a = args[3] !== undefined ? args[3] : 1; _stroke = `rgba(${floor(args[0]*255)},${floor(args[1]*255)},${floor(args[2]*255)},${a})`; }}
    }}
    function noStroke() {{ _stroke = null; }}
    function strokeWeight(w) {{ _lineWidth = w; }}
    function translate(x, y) {{ ctx.translate(x, y); }}
    function rotate(a) {{ ctx.rotate(a); }}
    function push() {{ ctx.save(); }}
    function pop() {{ ctx.restore(); }}
    function circle(x, y, r) {{
      ctx.beginPath(); ctx.arc(x, y, r / 2, 0, TAU);
      if (_fill) {{ ctx.fillStyle = _fill; ctx.fill(); }}
      if (_stroke) {{ ctx.strokeStyle = _stroke; ctx.lineWidth = _lineWidth; ctx.stroke(); }}
    }}
    function rect(x, y, w, h) {{
      if (_fill) {{ ctx.fillStyle = _fill; ctx.fillRect(x, y, w, h); }}
      if (_stroke) {{ ctx.strokeStyle = _stroke; ctx.lineWidth = _lineWidth; ctx.strokeRect(x, y, w, h); }}
    }}
    function line(x1, y1, x2, y2) {{
      if (!_stroke) return;
      ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2);
      ctx.strokeStyle = _stroke; ctx.lineWidth = _lineWidth; ctx.stroke();
    }}
    function beginShape() {{ _shapePts = []; _shapeActive = true; }}
    function vertex(x, y) {{ if (_shapeActive) _shapePts.push([x, y]); }}
    function endShape() {{
      if (!_shapeActive || _shapePts.length < 2) return;
      ctx.beginPath();
      ctx.moveTo(_shapePts[0][0], _shapePts[0][1]);
      for (let i = 1; i < _shapePts.length; i++) ctx.lineTo(_shapePts[i][0], _shapePts[i][1]);
      if (_fill) {{ ctx.fillStyle = _fill; ctx.fill(); }}
      if (_stroke) {{ ctx.strokeStyle = _stroke; ctx.lineWidth = _lineWidth; ctx.stroke(); }}
      _shapeActive = false;
    }}
    function hsl(h, s, l, a = 1.0) {{
      const H = ((h % 1) + 1) % 1 * 360, S = s * 100, L = l * 100;
      return `hsla(${H}, ${S}%, ${L}%, ${a})`;
    }}
    function param(name, def) {{ window[name] = def; }}

    // User script body
    {SKETCH_BODY}

    // Audio reactivity
    let audioCtx = null, analyser = null, freqData = null, bass = 0.5;
    document.getElementById("btn-audio").addEventListener("click", async () => {{
      if (!audioCtx) {{
        audioCtx = new (window.AudioContext || window.webkitAudioContext)();
        try {{
          const stream = await navigator.mediaDevices.getUserMedia({{ audio: true }});
          const src = audioCtx.createMediaStreamSource(stream);
          analyser = audioCtx.createAnalyser();
          analyser.fftSize = 64;
          src.connect(analyser);
          freqData = new Uint8Array(analyser.frequencyBinCount);
          document.getElementById("btn-audio").innerText = "mic active";
        }} catch(e) {{
          document.getElementById("btn-audio").innerText = "mic denied";
        }}
      }}
    }});

    const start = performance.now();
    function loop() {{
      const t = (performance.now() - start) * 0.001;
      if (analyser && freqData) {{
        analyser.getByteFrequencyData(freqData);
        bass = (freqData[1] + freqData[2]) / 255.0;
      }}
      ctx.save();
      if (typeof draw === 'function') draw(t);
      ctx.restore();
      requestAnimationFrame(loop);
    }}
    loop();
  </script>
</body>
</html>
"""

WEBGL_SHADER_TEMPLATE = """<!DOCTYPE html>
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
      z-index: 10;
    }}
    .badge-icon {{ width: 14px; height: 14px; border-radius: 50%; background: linear-gradient(135deg, #F57F66, #A9CDF1); }}
    .controls {{ position: absolute; bottom: 14px; left: 14px; display: flex; gap: 8px; z-index: 10; }}
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
  <a href="https://infinitedevices.org" target="_blank" rel="noopener" class="badge">
    <span class="badge-icon"></span>
    <span>{STYLE_LABEL} • made in <strong>infinite</strong></span>
  </a>

  <script>
    const canvas = document.getElementById("gl");
    const gl = canvas.getContext("webgl");
    if (!gl) {{ document.body.innerHTML = "webgl not supported"; }}

    const vsSource = `
      attribute vec2 position;
      void main() {{ gl_Position = vec4(position, 0.0, 1.0); }}
    `;

    const fsSource = `
      precision highp float;
      uniform vec2 res;
      uniform float t;
      uniform float aspect;
      uniform float audio_bass;
      uniform float audio_mid;
      uniform float audio_high;
      float atan2(float y, float x) {{ return atan(y, x); }}

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
      return shader;
    }}

    const program = gl.createProgram();
    gl.attachShader(program, createShader(gl, gl.VERTEX_SHADER, vsSource));
    gl.attachShader(program, createShader(gl, gl.FRAGMENT_SHADER, fsSource));
    gl.linkProgram(program);

    const positionBuffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1,-1, 1,-1, -1,1, -1,1, 1,-1, 1,1]), gl.STATIC_DRAW);

    const posLoc = gl.getAttribLocation(program, "position");
    gl.enableVertexAttribArray(posLoc);
    gl.vertexAttribPointer(posLoc, 2, gl.FLOAT, false, 0, 0);

    const uRes = gl.getUniformLocation(program, "res");
    const uTime = gl.getUniformLocation(program, "t");
    const uAspect = gl.getUniformLocation(program, "aspect");
    const uBass = gl.getUniformLocation(program, "audio_bass");
    const uMid = gl.getUniformLocation(program, "audio_mid");
    const uHigh = gl.getUniformLocation(program, "audio_high");

    let audioContext = null, analyser = null, freqData = null;
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
    parser.add_argument("--style", choices=list(TAXONOMY.keys()), help="Generate embed directly from art taxonomy")
    parser.add_argument("--field", help="Path to .field file")
    parser.add_argument("--glsl", help="Path to raw GLSL file")
    parser.add_argument("--sketch", help="Path to Sketch 2D JS file")
    parser.add_argument("--out", help="Destination HTML file")
    parser.add_argument("--all-styles", action="store_true", help="Generate embeds for all styles in taxonomy")
    parser.add_argument("--out-dir", default="website/embeds", help="Output directory for batch generation")
    args = parser.parse_args()

    if args.all_styles:
        out_dir = os.path.join(ROOT, args.out_dir)
        os.makedirs(out_dir, exist_ok=True)
        count = 0
        for sid, d in TAXONOMY.items():
            out_file = os.path.join(out_dir, f"{sid}.html")
            code = d.get("sketch_2d", "")
            rendered = (SKETCH_2D_TEMPLATE
                        .replace("{TITLE}", html.escape(d["name"]))
                        .replace("{STYLE_LABEL}", html.escape(d["name"]))
                        .replace("{SKETCH_BODY}", code))
            with open(out_file, "w") as f:
                f.write(rendered)
            count += 1
        print(f"Generated {count} interactive embeds in {args.out_dir}/")
        return

    if args.style:
        if not args.out:
            args.out = os.path.join(ROOT, "dist", "embeds", f"{args.style}.html")
        d = TAXONOMY[args.style]
        rendered = (SKETCH_2D_TEMPLATE
                    .replace("{TITLE}", html.escape(d["name"]))
                    .replace("{STYLE_LABEL}", html.escape(d["name"]))
                    .replace("{SKETCH_BODY}", d["sketch_2d"]))
        os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
        with open(args.out, "w") as f:
            f.write(rendered)
        print(f"Generated interactive embed for {d['name']}: {args.out}")
        return

    if args.field or args.glsl:
        shader_body = open(args.glsl).read() if args.glsl else json.load(open(args.field))["device"]["code"]
        rendered = (WEBGL_SHADER_TEMPLATE
                    .replace("{TITLE}", "Audio-Reactive Patch")
                    .replace("{STYLE_LABEL}", "field shader")
                    .replace("{SHADER_BODY}", shader_body))
        os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
        with open(args.out, "w") as f:
            f.write(rendered)
        print(f"Generated interactive shader embed: {args.out}")


if __name__ == "__main__":
    main()
