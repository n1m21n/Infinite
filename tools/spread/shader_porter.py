#!/usr/bin/env python3
"""infinite shader porter: transpile glsl/shadertoy shaders into infinite .field devices.

what: takes glsl code or shadertoy shader text, adapts coordinate spaces, injects
      audio-reactive parameters, packages it into a valid infinite .field device json,
      and generates a clean smallcase attribution message to the original artist.
"""

import argparse
import json
import os
import re
import sys


def transpile_glsl_to_field(glsl_code, audio_react=True):
    code = glsl_code

    match = re.search(r"void\s+mainImage\s*\(\s*out\s+vec4\s+(\w+)\s*,\s*in\s+vec2\s+(\w+)\s*\)\s*\{", code)
    if match:
        out_var = match.group(1)
        coord_var = match.group(2)
        code = code[match.end():]
        last_brace = code.rfind("}")
        if last_brace != -1:
            code = code[:last_brace]
        
        code = re.sub(rf"{coord_var}\s*/\s*iResolution\.xy", "uv", code)
        code = re.sub(rf"{coord_var}", "(uv * res)", code)
        code = re.sub(rf"{out_var}\s*=\s*", "col = ", code)

    code = re.sub(r"\biTime\b", "t", code)
    code = re.sub(r"\biResolution\b", "vec3(res.x, res.y, 1.0)", code)
    code = re.sub(r"\biGlobalTime\b", "t", code)
    code = re.sub(r"\batan\(([^,]+),\s*([^)]+)\)", r"atan2(\1, \2)", code)

    params_header = []
    if audio_react:
        params_header.extend([
            "// audio-reactive modulators",
            "param float audio_bass = 0.5 [0.0, 2.0];",
            "param float audio_mid = 0.5 [0.0, 2.0];",
            "param float audio_high = 0.5 [0.0, 2.0];",
            "param float reactivity = 1.0 [0.0, 3.0];",
            "",
            "// aspect-corrected coordinate space",
            "p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);",
            "",
        ])

    body = "\n".join(params_header) + code.strip() + "\nalpha = 1.0;\n"
    return body


def create_field_json(name, author, description, code):
    return {
        "format": "field",
        "version": 1,
        "meta": {
            "name": name.lower(),
            "author": (author or "community").lower(),
            "description": (description or f"audio-reactive field pixel device converted from {name}").lower(),
            "domain": "pixel"
        },
        "device": {
            "code": code,
            "params": {
                "audio_bass": 0.5,
                "audio_mid": 0.5,
                "audio_high": 0.5,
                "reactivity": 1.0
            },
            "nodeSettings": {
                "width": 1280.0,
                "height": 720.0,
                "animate": 1.0
            }
        }
    }


def generate_attribution_copy(name, author, out_filename):
    name_low = name.lower()
    author_low = author.lower()
    out_low = out_filename.lower()
    return f"""
----------------------------------------------------------------------
outreach and attribution template:
----------------------------------------------------------------------
hey @{author_low}, loved your "{name_low}" shader so much that i brought it
into infinite (an open-source audiovisual daw: https://infinitedevices.org).

it now runs as a real-time, audio-reactive device driven by synth modulation
and fft spectrum cables.

device preset: {out_low}
full source and free download: https://infinitedevices.org
----------------------------------------------------------------------
""".strip()


def main():
    parser = argparse.ArgumentParser(description="infinite shader porter")
    parser.add_argument("--name", required=True, help="name of the shader/preset")
    parser.add_argument("--author", default="shadertoy artist", help="original artist name/handle")
    parser.add_argument("--input", required=True, help="path to raw glsl file")
    parser.add_argument("--out", required=True, help="output .field file path")
    parser.add_argument("--desc", default="", help="description")

    args = parser.parse_args()

    if not os.path.exists(args.input):
        print(f"[!] input file {args.input} does not exist.")
        sys.exit(1)

    raw_glsl = open(args.input).read()
    transpiled_code = transpile_glsl_to_field(raw_glsl, audio_react=True)
    field_data = create_field_json(args.name, args.author, args.desc, transpiled_code)

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w") as f:
        json.dump(field_data, f, indent=2)

    print(f"generated infinite field device: {args.out}")
    print()
    print(generate_attribution_copy(args.name, args.author, os.path.basename(args.out)))


if __name__ == "__main__":
    main()
