# Copyright 2026 Timothy Place and the catenae contributors
# The composition rules for max_shortfall and max_lead, checked by brute force on a count model:
# leading and trailing decimators, L/M stages, interpolators and block stages, in chains of up
# to five. Throwaway; run by verify.sh. It must print "too small 0" on every line.
# Count model: each stage is a function from cumulative input frames to cumulative output
# frames (true of every stage here: its state is a function of how much it has been fed).
# For a chain, d(N) = O(N) - N*R. Cumulative from reset:
#   shortfall = max over N of -d(N)     lead = max over N of d(N)
# Rules under test, per stage, as functions of the arrival quantum q (frames arrive at the
# stage in whole multiples of q):
#   sum rule      (q ignored: q = 1 at every stage)
#   quantum rule  (q carried along the chain)
import itertools, random, sys
from fractions import Fraction as Fr
from math import gcd

class Up:
    def __init__(s, L): s.L = L; s.r = Fr(L); s.name = f"up{L}"
    def out(s, n): return s.L * n
    def short(s, q): return Fr(0)
    def lead(s, q): return Fr(0)
    def qout(s, q): return s.L * q
class DownTrail:            # writes on the LAST frame of a group (the old toy)
    def __init__(s, M): s.M = M; s.r = Fr(1, M); s.name = f"downT{M}"
    def out(s, n): return n // s.M
    def short(s, q): return Fr(s.M - gcd(q, s.M), s.M)
    def lead(s, q): return Fr(0)
    def qout(s, q): return q // s.M if q % s.M == 0 else 1
class DownLead:             # writes on the FIRST frame of a group (the carried decimators)
    def __init__(s, M): s.M = M; s.r = Fr(1, M); s.name = f"downL{M}"
    def out(s, n): return -((-n) // s.M)
    def short(s, q): return Fr(0)
    def lead(s, q): return Fr(s.M - gcd(q, s.M), s.M)
    def qout(s, q): return q // s.M if q % s.M == 0 else 1
class LMLead:               # L/M, output k written when input floor(kM/L) arrives: ceil(nL/M)
    def __init__(s, L, M): s.L = L; s.M = M; s.r = Fr(L, M); s.name = f"lmL{L}/{M}"
    def out(s, n): return -((-n * s.L) // s.M)
    def short(s, q): return Fr(0)
    def lead(s, q): return Fr(s.M - gcd(q * s.L, s.M), s.M)
    def qout(s, q): return q * s.L // s.M if (q * s.L) % s.M == 0 else 1
class LMTrail:
    def __init__(s, L, M): s.L = L; s.M = M; s.r = Fr(L, M); s.name = f"lmT{L}/{M}"
    def out(s, n): return (n * s.L) // s.M
    def short(s, q): return Fr(s.M - gcd(q * s.L, s.M), s.M)
    def lead(s, q): return Fr(0)
    def qout(s, q): return q * s.L // s.M if (q * s.L) % s.M == 0 else 1
class Block:
    def __init__(s, H): s.H = H; s.r = Fr(1); s.name = f"block{H}"
    def out(s, n): return (n // s.H) * s.H
    def short(s, q): return Fr(s.H - gcd(q, s.H))
    def lead(s, q): return Fr(0)
    def qout(s, q): return q if q % s.H == 0 else s.H

def lcm(a, b): return a * b // gcd(a, b)

def truth(chain):
    R = Fr(1)
    for st in chain: R *= st.r
    # period of d(N): safe upper bound, the product of every group-like number
    per = 1
    for st in chain:
        for v in (getattr(st, 'M', 1), getattr(st, 'H', 1), getattr(st, 'L', 1)): per = lcm(per, v)
    span = per * per if per * per <= 200000 else per * 8
    lo = hi = Fr(0)
    group = None
    for N in range(0, span + 1):
        x = N
        for st in chain: x = st.out(x)
        d = x - N * R
        lo = min(lo, d); hi = max(hi, d)
    return -lo, hi, R

def rule(chain, quantum):
    R = Fr(1)
    for st in chain: R *= st.r
    after = [Fr(1)] * len(chain)
    for i in range(len(chain) - 2, -1, -1): after[i] = after[i + 1] * chain[i + 1].r
    q = 1; s = Fr(0); l = Fr(0)
    for i, st in enumerate(chain):
        qq = q if quantum else 1
        s += st.short(qq) * after[i]; l += st.lead(qq) * after[i]
        q = st.qout(q)
    return s, l

def group_of(chain):
    R = Fr(1)
    for st in chain: R *= st.r
    n = 1
    while True:
        ok = True
        for k in range(1, 6):
            x = k * n
            for st in chain: x = st.out(x)
            if x != k * n * R: ok = False; break
        if ok: return n
        n += 1
        if n > 5000: return None

def check(chains, label):
    bad_sum = bad_q = 0; exact_sum = exact_q = 0; n = 0; worst = (0, None)
    bad_grp = 0
    for ch in chains:
        ts, tl, R = truth(ch)
        ss, sl = rule(ch, False); qs, ql = rule(ch, True)
        n += 1
        if ss < ts or sl < tl: bad_sum += 1
        if qs < ts or ql < tl:
            bad_q += 1
            if bad_q <= 5: print("  QUANTUM RULE TOO SMALL:", [s.name for s in ch], "true", ts, tl, "rule", qs, ql)
        if (ss, sl) == (ts, tl): exact_sum += 1
        if (qs, ql) == (ts, tl): exact_q += 1
        slack = (qs - ts) + (ql - tl)
        if slack > worst[0]: worst = (slack, [s.name for s in ch], ts, tl, qs, ql)
    print(f"{label}: {n} chains; sum rule too small {bad_sum}, exact {exact_sum}; quantum rule too small {bad_q}, exact {exact_q}; worst quantum slack {worst}")

if __name__ == "__main__":
    random.seed(1)
    pairs = [[Block(a), Block(b)] for a in range(1, 41) for b in range(1, 41)]
    check(pairs, "block pairs 1..40")
    for a, b in ((96, 128), (480, 512), (441, 512), (160, 256), (128, 192), (7, 5), (128, 64)):
        ts, tl, _ = truth([Block(a), Block(b)]); print(f"  {a}|{b}: true {ts}  sum {rule([Block(a),Block(b)],False)[0]}  quantum {rule([Block(a),Block(b)],True)[0]}")
    triples = [[Block(a), Block(b), Block(c)] for a in range(1, 17) for b in range(1, 17) for c in range(1, 17)]
    check(triples, "block triples 1..16")
    atoms = [lambda: Up(2), lambda: Up(3), lambda: DownTrail(2), lambda: DownTrail(3), lambda: DownLead(2), lambda: DownLead(3),
             lambda: DownLead(4), lambda: LMLead(3, 2), lambda: LMLead(2, 3), lambda: LMTrail(3, 2), lambda: LMTrail(2, 3),
             lambda: Block(2), lambda: Block(3), lambda: Block(4), lambda: Block(5), lambda: Block(6), lambda: Block(8), lambda: Block(12)]
    for length in (1, 2, 3):
        check([[a() for a in combo] for combo in itertools.product(atoms, repeat=length)], f"every chain of length {length} over {len(atoms)} stage kinds")
    check([[random.choice(atoms)() for _ in range(4)] for _ in range(6000)], "6000 random chains of length 4")
    check([[random.choice(atoms)() for _ in range(5)] for _ in range(3000)], "3000 random chains of length 5")
