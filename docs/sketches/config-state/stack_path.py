#!/usr/bin/env python3
# Copyright 2026 Timothy Place and the catenae contributors
"""The static worst-case stack of each call, from GCC's -fcallgraph-info=su output.

Usage: stack_path.py file.ci [file.ci ...]

For every function whose name starts a line of interest (a callback, a reset, a setter, a
post, a junction call) it sums the frames along the deepest path through the functions GCC
compiled in these files. A function it has no frame for (libm, memset) is outside the sum
and is named. Throwaway, as the rest of the sketch is.
"""
import re
import sys

frames, labels, edges = {}, {}, {}
for path in sys.argv[1:]:
    for line in open(path, encoding="utf-8"):
        node = re.match(r'node: \{ title: "([^"]+)" label: "(.*)" \}', line)
        if node:
            title, label = node.groups()
            size = re.search(r"(\d+) bytes \(static\)", label)
            symbol = title.split(":")[-1]
            if size:
                frames[symbol] = int(size.group(1))
            labels[symbol] = label.split("\\n")[0]
            continue
        edge = re.match(r'edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)"', line)
        if edge:
            source, target = (name.split(":")[-1] for name in edge.groups())
            edges.setdefault(source, set()).add(target)


def deepest(symbol, seen=()):
    if symbol in seen:
        sys.exit(f"recursion through {symbol}")
    own = frames.get(symbol)
    outside = set() if own is not None else {symbol}
    best, beyond = 0, set()
    for callee in edges.get(symbol, ()):
        depth, more = deepest(callee, seen + (symbol,))
        beyond |= more
        best = max(best, depth)
    return (own or 0) + best, outside | beyond


wanted = re.compile(r"(?<![:\w])(\w*callback\w*|reset_\w+|set_\w+|post_\w+|relay_\w+)\(")
for symbol, label in sorted(labels.items(), key=lambda item: item[1]):
    match = wanted.search(label)
    if not match or symbol not in frames:
        continue
    depth, outside = deepest(symbol)
    note = f"  plus {', '.join(sorted(outside))}" if outside else ""
    print(f"{depth:5d} bytes on the deepest path  {match.group(1)}{note}")
