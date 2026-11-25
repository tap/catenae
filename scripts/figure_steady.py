#!/usr/bin/env python3
# Copyright 2026 Timothy Place and the catenae contributors
"""Draws the guide's steady<> figure from the trace examples/bursts_and_buffering.cpp prints.

    catenae_example_bursts_and_buffering | scripts/figure_steady.py > guide/src/figures/steady.svg
    scripts/figure_steady.py TRACE [--out FILE]

A figure that shows behaviour is drawn by a script from the output of the example it
illustrates, and both are committed (PLAN.md section 11.1). The example's trace is CSV, one row a
call; lines that are not rows of it (the example's last line) are ignored. One style: the icon's
teal, each value labelled where it first appears, light and dark, and a text description.
"""
import argparse
import csv
import io
import sys
from html import escape

COLUMNS = ("call", "frames_in", "bare_out", "fifo_held", "steady_out")
LANES = (
    ("Frames the caller passes in", "frames_in", "64 on every call"),
    ("Frames the bare region writes", "bare_out", "nothing, then a whole hop of 128"),
    ("Frames waiting in steady<>'s FIFO after the call", "fifo_held",
     "it starts with 127 frames of silence: the added latency"),
    ("Frames steady<> hands back", "steady_out", "64 on every call"),
)
TOP = 128  # one scale for every lane, in frames
W, LEFT, RIGHT = 720, 24, 24
LANE_H, BAR_MAX, TITLE_H, GAP = 136, 56, 40, 28
STYLE = """<style>
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
</style>"""


def rows_of(text: str) -> list[dict[str, int]]:
    """The trace's rows: the header line and the lines after it that have its five fields."""
    lines = text.splitlines()
    start = next(i for i, line in enumerate(lines) if line.strip() == ",".join(COLUMNS))
    body = [line for line in lines[start + 1:] if line.count(",") == len(COLUMNS) - 1]
    reader = csv.DictReader(io.StringIO("\n".join([lines[start]] + body)))
    return [{key: int(row[key]) for key in COLUMNS} for row in reader]


def describe(rows: list[dict[str, int]]) -> str:
    """The text a reader who cannot see the figure gets."""
    bare = sorted({r["bare_out"] for r in rows})
    held = sorted({r["fifo_held"] for r in rows})
    return (f"{len(rows)} calls of 64 frames. The bare region writes "
            f"{' or '.join(str(v) for v in bare)} frames a call. Inside steady, a FIFO that starts with 127 "
            f"frames of silence holds {' or '.join(str(v) for v in held)} frames after each call, and 64 frames "
            f"are handed back on every call.")


def draw(rows: list[dict[str, int]]) -> str:
    """The SVG, one lane a column and one bar a call."""
    plot_w = W - LEFT - RIGHT
    slot = plot_w / len(rows)
    bar_w = min(44.0, slot - 2)
    height = 16 + len(LANES) * LANE_H + 30
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {height}" role="img" '
           f'aria-labelledby="t d" font-family="system-ui, -apple-system, Segoe UI, sans-serif">',
           '<title id="t">A bursting region and its steady form, call by call</title>',
           f'<desc id="d">{escape(describe(rows))}</desc>',
           STYLE,
           f'<rect class="surface" width="{W}" height="{height}"/>']
    for li, (title, key, note) in enumerate(LANES):
        y0 = 16 + li * LANE_H
        base = y0 + TITLE_H + GAP + BAR_MAX
        out.append(f'<text class="title" x="{LEFT}" y="{y0 + 14}">{escape(title)}</text>')
        out.append(f'<text class="note" x="{LEFT}" y="{y0 + 31}">{escape(note)}</text>')
        out.append(f'<line class="base" x1="{LEFT}" y1="{base}" x2="{W - RIGHT}" y2="{base}"/>')
        seen: set[int] = set()
        for ci, row in enumerate(rows):
            value = row[key]
            cx = LEFT + slot * (ci + 0.5)
            x = cx - bar_w / 2
            h = BAR_MAX * value / TOP
            tip = f"call {row['call']}: {value} frames"
            if value == 0:
                out.append(f'<line class="none" x1="{x + 8:.1f}" y1="{base - 1}" x2="{x + bar_w - 8:.1f}" '
                           f'y2="{base - 1}"><title>{tip}</title></line>')
            else:
                r = min(4.0, h)
                # a bar anchored to the baseline, rounded at its data end only
                out.append(f'<path class="bar" d="M{x:.1f} {base} V{base - h + r:.1f} Q{x:.1f} {base - h:.1f} '
                           f'{x + r:.1f} {base - h:.1f} H{x + bar_w - r:.1f} Q{x + bar_w:.1f} {base - h:.1f} '
                           f'{x + bar_w:.1f} {base - h + r:.1f} V{base} Z"><title>{tip}</title></path>')
            if value not in seen:  # each distinct value labelled once, where it first appears
                seen.add(value)
                out.append(f'<text class="value" x="{cx:.1f}" y="{base - h - 6:.1f}" '
                           f'text-anchor="middle">{value}</text>')
    axis_y = 16 + len(LANES) * LANE_H + 14
    for ci, row in enumerate(rows):
        cx = LEFT + slot * (ci + 0.5)
        out.append(f'<text class="axis" x="{cx:.1f}" y="{axis_y}" text-anchor="middle">call {row["call"]}</text>')
    out.append("</svg>")
    return "\n".join(out) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("trace", nargs="?", help="the example's output (default: standard input)")
    parser.add_argument("--out", help="where to write the SVG (default: standard output)")
    args = parser.parse_args()
    text = open(args.trace, encoding="utf-8").read() if args.trace else sys.stdin.read()
    svg = draw(rows_of(text))
    if args.out:
        with open(args.out, "w", encoding="utf-8", newline="\n") as f:
            f.write(svg)
    else:
        sys.stdout.write(svg)
    return 0


if __name__ == "__main__":
    sys.exit(main())
