#!/usr/bin/env python3
# Copyright 2026 Timothy Place and the catenae contributors
"""The size probes and their ceilings (PLAN.md sections 8 and 10, C0a).

Carried from SampleRateTap's scripts/ceilings.py (NOTICE.md): the map and call-graph readers,
the disassembly bound for the runtime's routines and the ceiling rule are its; the figures
are the ones C0a names, and the probe sets are catenae's.

    ceilings.py measure --key KEY --build-dir DIR [--ceilings FILE] [--record FILE] [--update]
    ceilings.py docs [--check]

Measures every probe set of tools/../bench (PROBES below) in a MinSizeRel build tree and gates
each figure against bench/ceilings.json, one-sided: a figure above its ceiling fails, one below
passes (a ceiling is a bound, not a pin). Per key and probe set:

  image.text, .rodata,   the output sections of the size probe (catenae_probe_<set>), as
  .data, .bss            `size -A` gives them: the whole image, runtime included
  core.text, .rodata,    the input sections the link kept from the compiled core's archive
  .data, .bss            members (libcatenae.a), from the link map
  unit.text, .rodata,    the same from the probe's own translation unit, which holds the
  .data, .bss            header templates it instantiates, beside its harness (main, the root)
  startup.functions      how many start-up functions (.init_array, .preinit_array entries)
                         catenae's own objects add: none, by D34 and CATENAE_CONSTINIT
  startup.stack          the static worst-case stack of the image's start-up functions,
                         whoever added them (Arm keys)
  stack.<call>.static    each call's static worst path from its noinline root, probe_<call>,
                         through the call graph GCC writes under -fstack-usage
                         -fcallgraph-info=su: each function's frame summed along the deepest
                         chain, the runtime's precompiled routines bounded from their
                         disassembly (Arm keys)
  stack.<call>.measured  the high-water the stack probe (catenae_stack_<set>) paints and reads
                         under qemu-system-arm (tools/probe/stack_meter.h): the path taken
  code.<call>            the bytes of code the call can reach: the stack image's symbol sizes of
                         every function on its call graph from probe_<call>, the root (which
                         holds what was inlined into it) and the runtime's routines included
                         (Arm keys)
  heap.used              where the stack probe prints CATENAE_HEAP: the bytes the allocator
                         holds after start-up, which no section of the image shows (Arm keys)
  sizeof.<part>          where the stack probe prints CATENAE_SIZEOF: a chain's state and
                         workspace, as the target lays them out (Arm keys)

--update records each figure and its ceiling: the figure plus 3 %, rounded up to a multiple of
64 bytes for sizes and of 8 (the AAPCS stack alignment) for stack, and 0 for 0 (a count of
start-up functions is a count: its ceiling is itself). --record writes the measured figures
as JSON whatever the verdict, so a run that fails still hands back the numbers. `docs`
rewrites the table between the ceilings markers in bench/README.md; --check fails if that
would change it.
"""
import argparse
import glob
import json
import math
import os
import pathlib
import re
import shutil
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
# The probe sets (bench/CMakeLists.txt, catenae_add_probe) and each one's calls: the stack
# probe prints CATENAE_STACK <call> <bytes> and roots each call at catenae_probe::probe_<call>.
PROBES = {"version": ["version"], "strip": ["reset", "process"], "strip_object": ["process"],
          "c1": ["reset", "process"], "c2": ["reset", "process"], "fft": ["forward", "inverse", "make"],
          "recursive": ["reset", "process"], "fir": ["reset", "process"],
          "designers": ["cookbook", "butterworth_2", "butterworth_4", "butterworth_8", "butterworth_16", "fir_63",
                        "fir_q15_255", "fir_1023"]}
# A call's root, catenae_probe::probe_<call>, as a mangled name or a call graph's title: anchored,
# so a static constructor whose name embeds the root's (_GLOBAL__sub_I_...) is not taken for it.
ROOT = r"(?:^|:)_ZN13catenae_probe\d+probe_{call}E"
# The probe sets whose start-up may have no static stack bound, and why: the guide's strawman,
# whose constructor calls operator new, which calls the new-handler through a pointer. Any other
# probe whose start-up cannot be bounded fails.
UNBOUNDED_STARTUP = {"strip_object": "operator new calls the new-handler through a pointer"}
# The probe sets that call the compiled core (the rest are header templates alone).
CALLS_CORE = {"version", "c2", "fft", "designers"}
# The keys, as the ratchet names them, and their QEMU machines (Arm) or none (Hexagon).
KEYS = {"m4-softfp": "mps2-an386", "m4f": "mps2-an386", "m33": "mps2-an505", "m55": "mps3-an547",
        "hexagon": None}
SIZE_GRAIN = 64
STACK_GRAIN = 8
MARGIN = 0.03
BEGIN = "<!-- ceilings:begin (scripts/ceilings.py docs; do not edit by hand) -->"
END = "<!-- ceilings:end -->"
SECTIONS = ("text", "rodata", "data", "bss")


def ceiling(figure: str, measured: int) -> int:
    if figure == "startup.functions" or measured == 0:
        return measured
    grain = STACK_GRAIN if figure.startswith(("stack.", "startup.stack")) else SIZE_GRAIN
    return int(math.ceil(measured * (1 + MARGIN) / grain)) * grain


def tool(key: str, name: str) -> str:
    if key == "hexagon":
        root = os.environ.get("HEXAGON_TOOLCHAIN_ROOT", "")
        candidates = [os.path.join(root, "bin", f"llvm-{name}"), f"llvm-{name}"]
    else:
        candidates = [f"arm-none-eabi-{name}"]
    for candidate in candidates:
        if shutil.which(candidate) or os.access(candidate, os.X_OK):
            return candidate
    raise SystemExit(f"no {name} for {key} (tried {', '.join(candidates)})")


def find_binary(build_dir: str, name: str) -> str:
    hits = [p for p in glob.glob(os.path.join(build_dir, "**", name), recursive=True)
            if os.path.isfile(p) and os.access(p, os.X_OK)]
    if len(hits) != 1:
        raise SystemExit(f"expected one {name} under {build_dir}, found {len(hits)}")
    return hits[0]


def map_inputs(map_path: str) -> list[tuple[str, int, str]]:
    """(input section, bytes, origin file) of every input section the link kept, from a GNU ld
    map (the Arm keys) or an lld one (Hexagon). SampleRateTap's reader."""
    lines = pathlib.Path(map_path).read_text().splitlines()
    inputs = []
    if lines and lines[0].split()[:4] == ["VMA", "LMA", "Size", "Align"]:
        for line in lines[1:]:
            m = re.match(r"^\s*[0-9a-f]+\s+[0-9a-f]+\s+([0-9a-f]+)\s+\d+\s+(\S.*):\((\.[^)]*)\)$", line)
            if m:
                inputs.append((m.group(3), int(m.group(1), 16), m.group(2)))
        return inputs
    try:
        start = lines.index("Linker script and memory map")  # before it: the discarded inputs
    except ValueError:
        raise SystemExit(f"{map_path}: neither a GNU ld nor an lld map")
    pending = None
    for line in lines[start:]:
        m = re.match(r"^ (\.\S+|COMMON)(?:\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*))?$", line)
        if m and m.group(2) is None:
            pending = m.group(1)  # a long name: address, size and file on the next line
            continue
        if m:
            inputs.append((m.group(1), int(m.group(3), 16), m.group(4)))
        else:
            cont = re.match(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$", line)
            if cont and pending:
                inputs.append((pending, int(cont.group(2), 16), cont.group(3)))
        pending = None
    return inputs


def size_rows(size_tool: str, binary: str) -> dict[str, int]:
    out = subprocess.run([size_tool, "-A", binary], check=True, capture_output=True, text=True).stdout
    rows = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0].startswith(".") and parts[1].isdigit():
            rows[parts[0]] = int(parts[1])
    return rows


def kind(section: str) -> str | None:
    for name in SECTIONS:
        if section == f".{name}" or section.startswith(f".{name}."):
            return name
    if section == "COMMON" or section.startswith(".sbss"):
        return "bss"
    if section.startswith(".sdata"):
        return "data"
    return None


def origin_of(origin: str, probe: str) -> str | None:
    if "libcatenae.a" in origin:
        return "core"
    if f"{probe}.dir" in origin:
        return "unit"
    return None


def measure_sizes(key: str, probe: str, binary: str) -> dict[str, int]:
    rows = size_rows(tool(key, "size"), binary)
    fig = {f"image.{s}": rows.get(f".{s}", 0) for s in SECTIONS}
    parts = {"core": {s: 0 for s in SECTIONS}, "unit": {s: 0 for s in SECTIONS}}
    startup = 0
    for section, size, origin in map_inputs(binary + ".map"):
        part = origin_of(origin, probe)
        if part is None:
            continue
        if section.startswith((".init_array", ".preinit_array", ".ctors")):
            startup += size // 4
        k = kind(section)
        if k:
            parts[part][k] += size
    # Every probe's unit holds its main; a probe that calls the compiled core must find it too.
    if parts["unit"]["text"] == 0 or (probe.removeprefix("catenae_probe_") in CALLS_CORE and parts["core"]["text"] == 0):
        raise SystemExit(f"{binary}.map: no input from the core or the probe's unit; the map format moved?")
    for part, sizes in parts.items():
        fig.update({f"{part}.{s}": sizes[s] for s in SECTIONS})
    fig["startup.functions"] = startup
    return fig


def parse_callgraphs(paths: list[str]):
    """Frames, external routines and edges of GCC's .ci files, merged: a function with a frame
    in one graph is no longer external because another graph calls it."""
    frames, externals, edges = {}, set(), {}
    for path in paths:
        text = pathlib.Path(path).read_text()
        for title, label in re.findall(r'node: \{ title: "([^"]*)" label: "((?:[^"\\]|\\.)*)"', text):
            m = re.search(r"(\d+) bytes \(([^)]*)\)", label)
            if m is None:
                externals.add(title)
            elif m.group(2) != "static":
                raise SystemExit(f"{path}: {title} has a {m.group(2)} frame; the bound needs static ones")
            else:
                frames[title] = int(m.group(1))
        for src, dst in re.findall(r'edge: \{ sourcename: "([^"]*)" targetname: "([^"]*)"', text):
            edges.setdefault(src, set()).add(dst)
    # A function GCC compiled is titled "<file>:<symbol>" in its own graph, and a call from another
    # file names it by its symbol alone: the symbol stands for the titled node, so a call from the
    # probe into the compiled core walks the core's own frames.
    for title in list(frames):
        symbol = title.rsplit(":", 1)[-1]
        if symbol != title and symbol not in frames:
            frames[symbol] = frames[title]
            edges.setdefault(symbol, set()).update(edges.get(title, ()))
    return frames, externals - set(frames), edges


def count_regs(reglist: str) -> tuple[int, int]:
    """(core, vfp double) registers named in an objdump register list."""
    core = vfp = 0
    for item in reglist.split(","):
        item = item.strip()
        lo, _, hi = item.partition("-")
        n = int(hi[1:]) - int(lo[1:]) + 1 if hi else 1
        if lo.startswith("d"):
            vfp += n
        elif lo.startswith("s"):
            vfp += n / 2
        else:
            core += n
    return core, int(math.ceil(vfp))


def runtime_depths(binary: str):
    """Worst-path stack bytes of each function in a linked Arm image, from its disassembly:
    every push, vpush, pre-decrementing store and sub from sp is summed (a function that
    pushes on two branches is charged both, so the figure only errs high), plus the deepest
    callee by bl or tail branch. Used for the runtime's precompiled routines; GCC's own
    frames cover the compiled code. SampleRateTap's bound."""
    dis = subprocess.run(["arm-none-eabi-objdump", "-d", "--no-show-raw-insn", binary], check=True,
                         capture_output=True, text=True).stdout
    funcs, cur = {}, None
    for line in dis.splitlines():
        head = re.match(r"^([0-9a-f]+) <([^>]+)>:$", line)
        if head:
            cur = int(head.group(1), 16)
            funcs[cur] = {"name": head.group(2), "frame": 0, "calls": set(), "unbounded": []}
            continue
        ins = re.match(r"^\s*[0-9a-f]+:\s+(\S+)\s*(.*)$", line)
        if cur is None or not ins:
            continue
        # objdump's comment follows ";" or "@" ("sub sp, #52 @ 0x34"): it is not an operand.
        mnem, ops = ins.group(1), ins.group(2).split(";")[0].split("@")[0].strip()
        f = funcs[cur]
        regs = re.search(r"\{([^}]*)\}", ops)
        if mnem.startswith(("push", "vpush")) or (mnem.startswith(("stmdb", "vstmdb")) and ops.startswith("sp!")):
            core, vfp = count_regs(regs.group(1))
            f["frame"] += 4 * core + 8 * vfp
        elif m := re.search(r"\[sp, #-(\d+)\]!", ops):
            f["frame"] += int(m.group(1))
        elif re.match(r"^sub[sw]?(\.w)?$", mnem) and re.match(r"^sp,", ops):
            m = re.search(r"#(\d+)$", ops)
            if m is None:
                f["unbounded"].append(f"{mnem} {ops}")
            else:
                f["frame"] += int(m.group(1))
        elif mnem.startswith("blx") and not re.search(r"<", ops):
            f["unbounded"].append(f"{mnem} {ops}")
        elif re.match(r"^b[a-z]*(\.[wn])?$", mnem) and re.match(r"^[0-9a-f]+ <[^+>]+>$", ops):
            target = int(ops.split()[0], 16)
            if target != cur:
                f["calls"].add(target)
    memo, active = {}, set()

    def depth(addr):
        if addr in memo:
            return memo[addr]
        f = funcs.get(addr)
        if f is None:
            raise SystemExit(f"{binary}: a call to {addr:#x}, which is no function's start")
        if f["unbounded"]:
            raise SystemExit(f"{binary}: {f['name']} has no static bound: {f['unbounded']}")
        if addr in active:
            raise SystemExit(f"{binary}: recursion through {f['name']}")
        active.add(addr)
        memo[addr] = f["frame"] + max((depth(c) for c in f["calls"]), default=0)
        active.discard(addr)
        return memo[addr]

    symbols = {}
    for line in subprocess.run(["arm-none-eabi-nm", binary], check=True, capture_output=True,
                               text=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in "TtWw":
            symbols.setdefault(parts[2], int(parts[0], 16) & ~1)
    by_address = {v: k for k, v in symbols.items()}

    def by_name(name):
        return depth(symbols[name]) if name in symbols else None

    def by_addr(addr):
        return depth(addr & ~1), by_address.get(addr & ~1, hex(addr))

    return by_name, by_addr


def code_bytes(binary: str, call: str) -> tuple[int, int]:
    """(bytes, functions) of the code a call can reach in a linked Arm image: every function
    reached from its root, probe_<call>, by a direct call or tail branch in the disassembly,
    the root and the runtime's routines included, each counted once at its symbol size."""
    sizes, names = {}, {}
    for line in subprocess.run(["arm-none-eabi-nm", "-S", "--defined-only", binary], check=True,
                               capture_output=True, text=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[2] in "TtWw":
            addr = int(parts[0], 16) & ~1
            sizes[addr] = max(sizes.get(addr, 0), int(parts[1], 16))
            names.setdefault(parts[3], addr)
    roots = [a for n, a in names.items() if re.search(ROOT.format(call=call), n)]
    if len(roots) != 1:
        raise SystemExit(f"{binary}: expected one root for {call}, found {len(roots)}")
    dis = subprocess.run(["arm-none-eabi-objdump", "-d", "--no-show-raw-insn", binary], check=True,
                         capture_output=True, text=True).stdout
    calls, cur = {}, None
    for line in dis.splitlines():
        head = re.match(r"^([0-9a-f]+) <([^>]+)>:$", line)
        if head:
            cur = int(head.group(1), 16)
            calls.setdefault(cur, set())
            continue
        ins = re.match(r"^\s*[0-9a-f]+:\s+(\S+)\s*(.*)$", line)
        if cur is None or not ins:
            continue
        ops = ins.group(2).split(";")[0].strip()
        if re.match(r"^b[a-z]*(\.[wn])?$", ins.group(1)) and re.match(r"^[0-9a-f]+ <[^+>]+>$", ops):
            target = int(ops.split()[0], 16)
            if target != cur:
                calls[cur].add(target)
    seen, todo = set(), [roots[0]]
    while todo:
        addr = todo.pop()
        if addr not in seen:
            seen.add(addr)
            todo.extend(calls.get(addr, ()))
    return sum(sizes.get(a, 0) for a in seen), len(seen)


def worst_path(frames, externals, edges, root, runtime):
    """(bytes, external routines reached) of the deepest path from root: GCC's frames for the
    compiled code, the disassembly's for the runtime."""
    memo, active = {}, set()

    def walk(node):
        if node in memo:
            return memo[node]
        if node in externals or node not in frames:
            # The graph is written before codegen, which can lower a call away: a routine the
            # image does not hold is called by nothing in it, or the link would have failed.
            name = node.rsplit(":", 1)[-1]
            d = runtime(name)
            memo[node] = (0, set()) if d is None else (d, {name})
            return memo[node]
        if node in active:
            raise SystemExit(f"recursion through {node}: no static bound")
        active.add(node)
        deepest, reached = 0, set()
        for callee in edges.get(node, ()):
            d, r = walk(callee)
            deepest = max(deepest, d)
            reached |= r
        active.discard(node)
        memo[node] = (frames[node] + deepest, reached)
        return memo[node]

    return walk(root)


def startup_stack(size_binary: str, by_addr) -> tuple[int, list[str]]:
    """The deepest static path among the image's start-up functions."""
    out = subprocess.run(["arm-none-eabi-objdump", "-s", "-j", ".preinit_array", "-j", ".init_array",
                          size_binary], capture_output=True, text=True).stdout
    entries = []
    for line in out.splitlines():
        m = re.match(r"^ ([0-9a-f]+) ((?:[0-9a-f]{8} ?){1,4})", line)
        if m:
            for word in m.group(2).split():
                entries.append(int.from_bytes(bytes.fromhex(word), "little"))
    deepest, names = 0, []
    for entry in entries:
        d, name = by_addr(entry)
        deepest = max(deepest, d)
        names.append(name)
    return deepest, names


def measure_stack(key: str, build_dir: str, probe_set: str, size_binary: str) -> dict[str, int]:
    name = f"catenae_stack_{probe_set}"
    binary = find_binary(build_dir, name)
    proc = subprocess.run(["qemu-system-arm", "-M", KEYS[key], "-nographic", "-semihosting", "-kernel", binary],
                          capture_output=True, text=True, timeout=600)
    out = proc.stdout + proc.stderr
    if "CATENAE_PROBE_DONE ok=1" not in out:
        print(out, file=sys.stderr)
        raise SystemExit(f"{binary}: the probe did not complete cleanly")
    painted = {m.group(1): int(m.group(2)) for m in re.finditer(r"CATENAE_STACK (\w+) (\d+)", out)}
    graphs = glob.glob(os.path.join(build_dir, "**", f"{name}.dir", "**", "*.ci"), recursive=True)
    graphs += glob.glob(os.path.join(build_dir, "**", "catenae.dir", "**", "*.ci"), recursive=True)
    if not any(f"{name}.dir" in g for g in graphs):
        raise SystemExit(f"no call graph for {name} (built with GCC's -fcallgraph-info=su?)")
    frames, externals, edges = parse_callgraphs(graphs)
    by_name, by_addr = runtime_depths(binary)
    fig = {}
    for call in PROBES[probe_set]:
        roots = [t for t in frames if re.search(ROOT.format(call=call), t)]
        if not roots or call not in painted:
            raise SystemExit(f"{name}: no root or no painted figure for {call}")
        bound, reached = max(worst_path(frames, externals, edges, r, by_name) for r in roots)
        if painted[call] > bound:
            # The bound is every frame on every path, the runtime's included: a deeper painted
            # figure means it missed one.
            raise SystemExit(f"{name} {call}: painted {painted[call]} B exceeds the static bound {bound} B")
        fig[f"stack.{call}.static"] = bound
        fig[f"stack.{call}.measured"] = painted[call]
        code, functions = code_bytes(binary, call)
        fig[f"code.{call}"] = code
        print(f"{name} {call}: static {bound} B, measured {painted[call]} B; "
              f"runtime routines on the paths: {', '.join(sorted(reached)) or 'none'}; "
              f"code reached {code} B in {functions} function(s)")
    for part, size in re.findall(r"CATENAE_SIZEOF (\w+) (\d+)", out):
        fig[f"sizeof.{part}"] = int(size)
    heap = re.search(r"CATENAE_HEAP (\d+)", out)
    if heap:
        fig["heap.used"] = int(heap.group(1))
        print(f"{name}: the heap holds {fig['heap.used']} B after start-up")
    _, size_by_addr = runtime_depths(size_binary)
    try:
        deepest, names = startup_stack(size_binary, size_by_addr)
    except SystemExit as unbounded:
        if probe_set not in UNBOUNDED_STARTUP:
            raise
        # The strawman's start-up has no bound to record; that it has none is the finding.
        print(f"catenae_probe_{probe_set} start-up: no static bound ({UNBOUNDED_STARTUP[probe_set]}): {unbounded}")
        return fig
    fig["startup.stack"] = deepest
    print(f"catenae_probe_{probe_set} start-up: {len(names)} function(s) ({', '.join(names) or 'none'}), "
          f"deepest static path {deepest} B")
    return fig


def measure(key: str, build_dir: str) -> dict[str, dict[str, int]]:
    measured = {}
    for probe_set in PROBES:
        probe = f"catenae_probe_{probe_set}"
        binary = find_binary(build_dir, probe)
        fig = measure_sizes(key, probe, binary)
        if KEYS[key] is not None:
            fig.update(measure_stack(key, build_dir, probe_set, binary))
        measured[probe_set] = fig
    return measured


def cmd_measure(args) -> int:
    path = pathlib.Path(args.ceilings)
    doc = json.loads(path.read_text()) if path.exists() else {}
    measured = measure(args.key, args.build_dir)
    if args.record:
        record = pathlib.Path(args.record)
        rec = json.loads(record.read_text()) if record.exists() else {}
        rec[args.key] = measured
        record.write_text(json.dumps(rec, indent=2, sort_keys=True) + "\n")
    if args.update:
        doc[args.key] = {s: {f: {"recorded": v, "ceiling": ceiling(f, v)} for f, v in figs.items()}
                         for s, figs in measured.items()}
        path.write_text(json.dumps(doc, indent=2, sort_keys=True) + "\n")
        print(f"updated {path} ({args.key})")
        return 0
    base = doc.get(args.key, {})
    failures = []
    for probe_set, figs in measured.items():
        for figure, value in sorted(figs.items()):
            label = f"{args.key} {probe_set} {figure}"
            rec = base.get(probe_set, {}).get(figure)
            if rec is None:
                print(f"{label}: {value} (NO CEILING: record it with --update)")
                failures.append(label)
            elif value > rec["ceiling"]:
                print(f"{label}: {value} vs ceiling {rec['ceiling']} (recorded {rec['recorded']}) ABOVE CEILING")
                failures.append(label)
            else:
                print(f"{label}: {value} vs ceiling {rec['ceiling']} (recorded {rec['recorded']}) ok")
    # Every recorded ceiling must have been measured: a probe that vanished must not simply
    # stop being gated.
    for probe_set, figs in base.items():
        for figure in figs:
            if figure not in measured.get(probe_set, {}):
                print(f"{args.key} {probe_set} {figure}: recorded but NOT MEASURED")
                failures.append(f"{args.key} {probe_set} {figure}")
    for f in failures:
        print(f"FAILED: {f}")
    return 1 if failures else 0


def render(doc: dict) -> str:
    rows = []
    for probe_set in PROBES:
        figures = sorted({f for key in doc.values() for f in key.get(probe_set, {})},
                         key=lambda f: (["image", "core", "unit", "sizeof", "startup", "heap", "stack", "code"].index(f.split(".")[0]), f))
        keys = [k for k in KEYS if probe_set in doc.get(k, {})]
        if not keys:
            continue
        rows += [f"Probe `{probe_set}`: each cell is the recorded figure / its ceiling, in bytes "
                 "(start-up functions: a count).", "",
                 "| figure | " + " | ".join(f"`{k}`" for k in keys) + " |",
                 "|---|" + "--:|" * len(keys)]
        for figure in figures:
            cells = []
            for k in keys:
                rec = doc[k][probe_set].get(figure)
                cells.append(f"{rec['recorded']:,} / {rec['ceiling']:,}" if rec else "")
            rows.append(f"| `{figure}` | " + " | ".join(cells) + " |")
        rows.append("")
    return "\n".join(rows).rstrip()


def cmd_docs(args) -> int:
    doc = json.loads(pathlib.Path(args.ceilings).read_text())
    readme = REPO / "bench" / "README.md"
    text = readme.read_text()
    if BEGIN not in text or END not in text:
        raise SystemExit(f"{readme}: no ceilings markers")
    head, rest = text.split(BEGIN, 1)
    _, tail = rest.split(END, 1)
    new = f"{head}{BEGIN}\n{render(doc)}\n{END}{tail}"
    if args.check:
        if new != text:
            print(f"{readme}: the ceilings table is stale; run scripts/ceilings.py docs")
            return 1
        print(f"{readme}: the ceilings table matches bench/ceilings.json")
        return 0
    readme.write_text(new)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("measure")
    m.add_argument("--key", required=True, choices=sorted(KEYS))
    m.add_argument("--build-dir", required=True)
    m.add_argument("--ceilings", default=str(REPO / "bench" / "ceilings.json"))
    m.add_argument("--record", help="write the measured figures here as JSON, whatever the verdict")
    m.add_argument("--update", action="store_true")
    d = sub.add_parser("docs")
    d.add_argument("--ceilings", default=str(REPO / "bench" / "ceilings.json"))
    d.add_argument("--check", action="store_true")
    args = ap.parse_args()
    return cmd_measure(args) if args.cmd == "measure" else cmd_docs(args)


if __name__ == "__main__":
    sys.exit(main())
