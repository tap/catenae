#!/usr/bin/env bash
# Builds and runs the sketch on the host with each compiler it finds, at C++17, 20 and 23, and
# checks that every case of must_not_compile.cpp is refused with its own message.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out="${1:-/tmp/catenae-sketch}"
mkdir -p "$out"
flags="-fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wdouble-promotion -Wold-style-cast -Werror"

for compiler in g++ clang++; do
    command -v "$compiler" > /dev/null || continue
    for standard in 17 20 23; do
        for unit in demo trace; do
            "$compiler" -std=c++$standard -O2 $flags -DCATENAE_SKETCH_AUDIT "$unit.cpp" -o "$out/$unit"
        done
        "$compiler" -std=c++$standard -O2 $flags -pthread mailbox_stress.cpp -o "$out/stress"
        "$compiler" -std=c++$standard $flags -DCASE=0 -fsyntax-only must_not_compile.cpp
    done
    echo "$compiler: demo, trace and mailbox_stress build clean at C++17, 20 and 23"
done

"$out/demo" > "$out/demo.txt"
"$out/trace" | python3 figure.py | cmp - steady.svg
echo "demo runs (audit checks on); steady.svg matches trace"

expect() { # case, message
    local log="$out/case$1.log"
    if g++ -std=c++17 $flags -DCASE="$1" -fsyntax-only must_not_compile.cpp > "$log" 2>&1; then
        echo "case $1 compiled, and it must not"
        exit 1
    fi
    grep -q -F "$2" "$log" || { echo "case $1 failed without its message: $2"; exit 1; }
    echo "case $1 refused: $2"
}
expect 1 "catenae: one stage's output port is not the next stage's input port"
expect 2 "catenae: a fixed block must be a whole number of the chain's input groups"
expect 3 "catenae: a fixed block must be a whole number of the chain's input groups"
expect 4 "check_failed"
expect 5 "catenae: both sides of | must be stages"
expect 6 "catenae: the output block is too small for this input block"
expect 7 "catenae: a state is a plain aggregate"
expect 8 "catenae: a mailbox carries a trivially copyable value"
