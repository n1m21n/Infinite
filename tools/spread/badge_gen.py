#!/usr/bin/env python3
"""Infinite Dynamic SVG Badge Generator: Creates animated GitHub README audio visualizers.

What: Generates animated SVG badges and banners that render natively in GitHub Markdown
      and website READMEs. Features animated frequency bars / waveform spectrums and
      an embedded link back to https://infinitedevices.org.
Why:  Developers love putting dynamic widgets on their GitHub profile READMEs and repos.
      Every developer who embeds this badge provides permanent visibility and backlinks
      to infinitedevices.org directly inside GitHub's developer ecosystem.

Usage:
    python3 tools/spread/badge_gen.py --out website/assets/badges/visualizer.svg --theme midnight
"""

import argparse
import os


def generate_animated_svg(theme="midnight"):
    bg = "#151930" if theme == "midnight" else "#0d1117"
    border = "#2f3868"
    accent1 = "#F57F66"
    accent2 = "#A9CDF1"

    # 16 animated spectrum bars with varying CSS keyframe animations
    bars_svg = []
    styles = []

    bar_widths = 8
    gap = 6
    start_x = 20

    for i in range(18):
        x = start_x + i * (bar_widths + gap)
        delay = (i * 0.12) % 1.2
        duration = 0.8 + (i % 4) * 0.25
        h_min = 12 + (i % 5) * 4
        h_max = 50 + (i % 7) * 8

        styles.append(f"""
          @keyframes bounce_{i} {{
            0%, 100% {{ height: {h_min}px; y: {75 - h_min}px; }}
            50% {{ height: {h_max}px; y: {75 - h_max}px; }}
          }}
          .bar-{i} {{
            animation: bounce_{i} {duration:.2f}s ease-in-out infinite;
            animation-delay: -{delay:.2f}s;
          }}
        """)

        bars_svg.append(
            f'<rect class="bar bar-{i}" x="{x}" y="{75 - h_min}" width="{bar_widths}" height="{h_min}" rx="3" fill="url(#brandGrad)"/>'
        )

    css_block = "\n".join(styles)
    bars_block = "\n    ".join(bars_svg)

    svg = f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 460 96" width="460" height="96" fill="none">
  <defs>
    <linearGradient id="brandGrad" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0%" stop-color="{accent1}" />
      <stop offset="100%" stop-color="{accent2}" />
    </linearGradient>
    <style>
      .bg {{ fill: {bg}; stroke: {border}; stroke-width: 1.5; rx: 12px; }}
      .title {{ font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; font-size: 14px; font-weight: 700; fill: #ffffff; }}
      .sub {{ font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; font-size: 11px; font-weight: 500; fill: #8F99B8; }}
      .link {{ font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; font-size: 11px; font-weight: 600; fill: {accent1}; text-decoration: none; }}
      {css_block}
    </style>
  </defs>

  <!-- Background Card -->
  <rect class="bg" width="458" height="94" x="1" y="1" />

  <!-- Spectrum Bars -->
  <g id="bars">
    {bars_block}
  </g>

  <!-- Brand Typography -->
  <g transform="translate(280, 32)">
    <text class="title" x="0" y="0">infinite</text>
    <text class="sub" x="0" y="18">visuals, music and code</text>
    <a href="https://infinitedevices.org" target="_blank">
      <text class="link" x="0" y="38">infinitedevices.org</text>
    </a>
  </g>
</svg>
"""
    return svg


def main():
    parser = argparse.ArgumentParser(description="infinite animated svg badge generator")
    parser.add_argument("--out", default="website/assets/badges/visualizer.svg", help="output path")
    parser.add_argument("--theme", default="midnight", choices=["midnight", "dark"], help="theme mode")

    args = parser.parse_args()
    svg = generate_animated_svg(args.theme)

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w") as f:
        f.write(svg)

    print(f"generated animated badge svg: {args.out}")
    print()
    print("markdown snippet for github readmes or profiles:")
    print(f"[![infinite visualizer](https://infinitedevices.org/assets/badges/{os.path.basename(args.out)})](https://infinitedevices.org)")


if __name__ == "__main__":
    main()
