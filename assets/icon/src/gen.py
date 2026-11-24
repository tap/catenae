import math, sys

TEAL = "#1E9A98"
RIM_Y = 112.0

def sinc(x):
    return 1.0 if abs(x) < 1e-9 else math.sin(math.pi * x) / (math.pi * x)

def wave(x, boost, lobes):
    """Windowed sinc. boost>0 lifts the sidelobes for legibility (1/x decay kept);
    a raised-cosine taper over the last lobe lands the tail flat on the zero line."""
    a = 1.0 + boost * (1.0 - math.exp(-x * x))
    end = lobes + 1.0
    if x > end - 1.0:
        t = (x - (end - 1.0))
        a *= 0.5 * (1 + math.cos(math.pi * t)) if t < 1 else 0.0
    return sinc(x) * a

def tail_path(x0_px, unit, amp, boost, lobes, x_start=-1.0):
    pts = []
    n = 400
    x_end = lobes + 1.0
    for i in range(n + 1):
        x = x_start + (x_end - x_start) * i / n
        pts.append((x0_px + (x + 1.0) * unit, RIM_Y - amp * wave(x, boost, lobes)))
    return "M " + " L ".join(f"{px:.2f} {py:.2f}" for px, py in pts)

# Cat seen from behind, sitting in the glass; open at the bottom (the rim closes it).
CAT = ("M 56 112 "
       "C 48 94, 56 74, 76 68 "
       "C 71 60, 70 52, 72 44 "
       "L 74 25 L 87 37 "
       "Q 96 33, 105 37 "
       "L 118 25 L 120 44 "
       "C 122 52, 121 60, 116 68 "
       "C 133 74, 140 96, 128 112")

GLASS = ("M 34 112 L 156 112 L 95 166 Z M 95 166 L 95 192 "
         "M 72 194 Q 95 189, 118 194")

def svg(stroke=5.0, boost=1.3, lobes=3, unit=24.0, amp=74.0, size=None, bg=None):
    tail = tail_path(128.0, unit, amp, boost, lobes)
    right = 128 + (lobes + 2) * unit + stroke
    vb = f"20 10 {right - 20 + 4:.0f} 192"
    dims = f' width="{size}" height="{size * 192 / (right - 16):.0f}"' if size else ""
    bgrect = f'<rect x="0" y="0" width="1000" height="1000" fill="{bg}"/>' if bg else ""
    mask = ('<defs><mask id="behind" maskUnits="userSpaceOnUse" x="0" y="0" width="1000" height="1000">'
            '<rect x="0" y="0" width="1000" height="1000" fill="#fff"/>'
            f'<path d="{CAT} Z" fill="#000"/></mask></defs>')
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{vb}"{dims}>{mask}{bgrect}'
            f'<g fill="none" stroke="{TEAL}" stroke-width="{stroke}" '
            f'stroke-linecap="round" stroke-linejoin="round">'
            f'<path d="{GLASS}"/><path d="{CAT}"/><path d="{tail}" mask="url(#behind)"/></g></svg>')

if __name__ == "__main__":
    which = sys.argv[1]
    if which == "master":
        print(svg())
    elif which == "true":
        print(svg(boost=0.0))
    elif which == "small":
        print(svg(stroke=11, boost=1.1, lobes=2, unit=28, amp=76))

def inner_tail(x0_px, unit, amp, boost, lobes, w0=10.0, x_end=0.95):
    """Second tail edge: offset inward (toward the rim) along the main lobe's
    normal, tapering to zero so it merges back into the sinc near the zero crossing."""
    n = 300
    xs = [-1.0 + (x_end + 1.0) * i / n for i in range(n + 1)]
    P = [(x0_px + (x + 1) * unit, RIM_Y - amp * wave(x, boost, lobes)) for x in xs]
    out = []
    for i, (px, py) in enumerate(P):
        a, b = P[max(i - 1, 0)], P[min(i + 1, n)]
        tx, ty = b[0] - a[0], b[1] - a[1]
        L = math.hypot(tx, ty) or 1
        nx, ny = ty / L, -tx / L          # right-hand normal = below/inside the lobe
        s = i / n
        w = w0 * math.cos(s * math.pi / 2) ** 1.5
        out.append((px - nx * w, py - ny * w))
    return "M " + " L ".join(f"{px:.2f} {py:.2f}" for px, py in out)

def svg2(stroke=5.0, boost=1.3, lobes=3, unit=24.0, amp=74.0, w0=10.0):
    base = svg(stroke, boost, lobes, unit, amp)
    extra = f'<path d="{inner_tail(128.0, unit, amp, boost, lobes, w0)}" mask="url(#behind)"/>'
    return base.replace('</g></svg>', extra + '</g></svg>')

if __name__ == "__main__" and sys.argv[1] in ("double", "double_small"):
    print(svg2() if sys.argv[1] == "double" else svg2(stroke=11, boost=1.1, lobes=2, unit=28, amp=76, w0=16))

def parallel_tail(x0_px, unit, amp, boost, lobes, w=11.0, x_end=0.35):
    """Constant-offset second line on the outside of the main lobe, ending open."""
    n = 300
    xs = [-1.0 + (x_end + 1.0) * i / n for i in range(n + 1)]
    P = [(x0_px + (x + 1) * unit, RIM_Y - amp * wave(x, boost, lobes)) for x in xs]
    out = []
    for i, (px, py) in enumerate(P):
        a, b = P[max(i - 1, 0)], P[min(i + 1, n)]
        tx, ty = b[0] - a[0], b[1] - a[1]
        L = math.hypot(tx, ty) or 1
        nx, ny = ty / L, -tx / L
        out.append((px + nx * w, py + ny * w))
    return "M " + " L ".join(f"{px:.2f} {py:.2f}" for px, py in out)

def svg3(w=11.0, x_end=0.35, stroke=5.0, boost=1.3, lobes=3, unit=24.0, amp=74.0):
    base = svg(stroke, boost, lobes, unit, amp)
    extra = f'<path d="{parallel_tail(128.0, unit, amp, boost, lobes, w, x_end)}" mask="url(#behind)"/>'
    return base.replace('</g></svg>', extra + '</g></svg>')

if __name__ == "__main__" and sys.argv[1] == "parallel":
    print(svg3(w=float(sys.argv[2]), x_end=float(sys.argv[3])))
