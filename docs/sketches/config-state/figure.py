# Copyright 2026 Timothy Place and the catenae contributors
"""Draws steady.svg from the output of trace.cpp:  ./trace | python3 figure.py > steady.svg"""
import csv
import sys
from html import escape

rows = list(csv.DictReader(sys.stdin))
calls = [int(r["call"]) for r in rows]
lanes = [
    ("Frames the caller passes in", "frames_in", "64 on every call"),
    ("Frames the bare region writes", "bare_out", "nothing, then a whole block"),
    ("Frames waiting in steady<>'s FIFO after the call", "fifo_held",
     "starts with 127 frames of silence: the added latency"),
    ("Frames steady<> hands back", "steady_out", "64 on every call"),
]
TOP = 128  # one scale for every lane, in frames

W, LEFT, RIGHT = 720, 24, 24
LANE_H, BAR_MAX, TITLE_H, GAP = 136, 56, 40, 28
plot_w = W - LEFT - RIGHT
slot = plot_w / len(calls)
bar_w = min(44.0, slot - 2)
H = 16 + len(lanes) * LANE_H + 30

out = []
out.append(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" role="img" '
           f'aria-labelledby="t d" font-family="system-ui, -apple-system, Segoe UI, sans-serif">')
out.append('<title id="t">A bursting region and its steady form, call by call</title>')
out.append('<desc id="d">Eight calls of 64 frames. The bare region writes 0 then 128 frames alternately. '
           'Inside steady, a FIFO that starts with 127 frames of silence holds 63 or 127 frames after each call, '
           'and 64 frames are handed back on every call.</desc>')
out.append("""<style>
  .surface { fill: #fcfcfb; }
  .title   { fill: #0b0b0b; font-size: 14px; font-weight: 600; }
  .note    { fill: #52514e; font-size: 12px; }
  .value   { fill: #0b0b0b; font-size: 12px; font-variant-numeric: tabular-nums; }
  .axis    { fill: #52514e; font-size: 12px; font-variant-numeric: tabular-nums; }
  .base    { stroke: #d9d8d2; stroke-width: 1; }
  .bar     { fill: #1E9A98; }
  .none    { stroke: #1E9A98; stroke-width: 2; stroke-linecap: round; }
  @media (prefers-color-scheme: dark) {
    .surface { fill: #1a1a19; }
    .title, .value { fill: #f0efec; }
    .note, .axis { fill: #c3c2b7; }
    .base { stroke: #3a3a37; }
  }
</style>""")
out.append(f'<rect class="surface" width="{W}" height="{H}"/>')

for li, (title, key, note) in enumerate(lanes):
    y0 = 16 + li * LANE_H
    base = y0 + TITLE_H + GAP + BAR_MAX
    out.append(f'<text class="title" x="{LEFT}" y="{y0 + 14}">{escape(title)}</text>')
    out.append(f'<text class="note" x="{LEFT}" y="{y0 + 31}">{escape(note)}</text>')
    out.append(f'<line class="base" x1="{LEFT}" y1="{base}" x2="{W - RIGHT}" y2="{base}"/>')
    seen = set()
    for ci, r in enumerate(rows):
        v = int(r[key])
        cx = LEFT + slot * (ci + 0.5)
        x = cx - bar_w / 2
        h = BAR_MAX * v / TOP
        tip = f'call {r["call"]}: {v} frames'
        if v == 0:
            out.append(f'<line class="none" x1="{x + 8:.1f}" y1="{base - 1}" x2="{x + bar_w - 8:.1f}" y2="{base - 1}">'
                       f'<title>{tip}</title></line>')
        else:
            rr = min(4.0, h)
            # a bar anchored to the baseline, rounded at its data end only
            out.append(f'<path class="bar" d="M{x:.1f} {base} V{base - h + rr:.1f} Q{x:.1f} {base - h:.1f} {x + rr:.1f} {base - h:.1f} '
                       f'H{x + bar_w - rr:.1f} Q{x + bar_w:.1f} {base - h:.1f} {x + bar_w:.1f} {base - h + rr:.1f} V{base} Z">'
                       f'<title>{tip}</title></path>')
        if v not in seen:  # label each distinct value once, where it first appears
            seen.add(v)
            out.append(f'<text class="value" x="{cx:.1f}" y="{base - h - 6:.1f}" text-anchor="middle">{v}</text>')

axis_y = 16 + len(lanes) * LANE_H + 14
for ci, c in enumerate(calls):
    cx = LEFT + slot * (ci + 0.5)
    out.append(f'<text class="axis" x="{cx:.1f}" y="{axis_y}" text-anchor="middle">call {c}</text>')
out.append("</svg>")
print("\n".join(out))
