import re, subprocess, sys
src, dst, margin = sys.argv[1], sys.argv[2], float(sys.argv[3])
s = open(src).read()
vx, vy, vw, vh = map(float, re.search(r'viewBox="([^"]+)"', s).group(1).split())
W = 2000; scale = W / vw
subprocess.run(["rsvg-convert", "-w", str(W), src, "-o", "/tmp/bb.png"], check=True)
g = subprocess.run(["convert", "/tmp/bb.png", "-trim", "-format", "%w %h %X %Y", "info:"],
                   capture_output=True, text=True).stdout.split()
w, h, X, Y = [float(t.replace('+', '')) for t in g]
bx, by, bw, bh = vx + X / scale, vy + Y / scale, w / scale, h / scale
side = max(bw, bh) * (1 + 2 * margin)
cx, cy = bx + bw / 2, by + bh / 2
vb = f'{cx - side/2:.2f} {cy - side/2:.2f} {side:.2f} {side:.2f}'
s = re.sub(r'viewBox="[^"]+"', f'viewBox="{vb}"', s)
open(dst, "w").write(s)
print(dst, f"content {bw:.0f}x{bh:.0f}", vb)
