<!-- Copyright 2026 Timothy Place and the catenae contributors -->

# Stages and chains

An audio program is a row of small steps: a level, a delay, a filter. Each step reads one
kind of block and writes another, and two neighbours must agree on what passes between
them. This chapter is about when a disagreement is found. Its constraint is a mistake that
should not survive compilation.

The example throughout is one stereo strip: a gain that ramps over 96 frames when its
level changes, a fixed delay of 32 frames, and a step that halves what comes in.

## What you would naturally write

You would write a node with a virtual `process`, and a graph that holds a list of nodes.
Each node reports how many channels it reads and how many it writes. The graph compares
the two numbers when one node is connected to the next.

```cpp
{{#include ../../examples/run_time_graph.h:node}}
```

The strip is then three nodes, joined when the program starts.

```cpp
{{#include ../../examples/run_time_graph_main.cpp:built}}
```

This is a sound design. A node of a new kind can be added without recompiling the graph,
and the order of the nodes can be decided as the program runs. Nothing here allocates: the
nodes are static, and the graph holds eight pointers.

Look at where the checking happens. `connect` compares the previous node's `outputs()`
with the next node's `inputs()` and returns `false` when they differ. The numbers it
compares are the results of virtual calls, so they do not exist until the program runs.
The answer is a `bool` that the caller must test. And `process` reaches each node through
a pointer: an indirect call for each node, for each block.

## What it costs on the target

The first guess is that the cost is speed, from those indirect calls. So that is what was
measured.

The harness runs the strip twice over 1000 blocks of 64 stereo frames: once as the graph
above (`strip_graph`), and once as the chain the next section writes (`strip_chain`). Both
are fed the same input. Both print the same checksum, `9edaac15a16f6c7f`, on every target:
the two produce the same output, bit for bit. The counts are instructions executed, or
packets on Hexagon.

| Target | `strip_chain` | `strip_graph` | chain less graph |
|---|--:|--:|--:|
| `m4-softfp` | 24,160,128 | 23,574,864 | +585,264 (+2.5 %) |
| `m4f` | 3,796,665 | 3,840,400 | -43,735 (-1.1 %) |
| `m33` | 3,860,694 | 3,841,430 | +19,264 (+0.5 %) |
| `m55` | 2,375,698 | 2,472,434 | -96,736 (-3.9 %) |
| `hexagon` | 1,172,150 packets | 1,299,888 packets | -127,738 (-9.8 %) |
| `host` | 2,777,174 | 3,203,166 | -425,992 (-13.3 %) |

The numbers are from
[bench run 45653902512](https://github.com/tap/catenae/actions/runs/45653902512), at commit
`66debaa`. The first five targets run under QEMU, and this reproduces one of them:

```sh
cmake -S . -B build-17 -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m33-mps2.cmake \
      -DCATENAE_BUILD_TESTS=OFF -DCATENAE_BUILD_EXAMPLES=OFF -DCATENAE_BUILD_BENCH=ON
cmake --build build-17
scripts/icount.py --key m33 --build-dir build-17 --plugin /tmp/libinsncount.so
```

A QEMU count is the whole run: start-up, making the input, the checksum and the `printf`.
The `host` count is the loop alone. Those other parts are the same in both binaries, so
the difference between the two columns is the strip's.

Read the last column honestly. On the four Cortex-M cores the two cost about the same,
within 4 % either way. On two of them the chain counts slightly more, and on two slightly
fewer. On Hexagon and on the x86-64 host the chain is cheaper. If you came to this chapter
expecting virtual calls to be ruinous on a small processor, these numbers do not say so,
and this guide will not say so either.

What the graph costs is not speed. It is where its mistakes are found.

- **They are found on the target, as the program runs.** A node with the wrong channel
  count is refused when `connect` is called. That call is in the image, on the device.
  The build that produced the image passed.
- **Every caller must handle the `false`.** The listing above keeps it in `joined`. Code
  that builds a graph has to decide what a device does when its own signal path will not
  join, and that code path has to be written and tested.
- **What `connect` does not compare is not found at all.** It compares channel counts and
  nothing else. `process` takes a `const float*` and a frame count. Nothing checks that
  the buffer behind the pointer is as long as the count says.

A desktop program can report such a failure and exit. But the mistake was already in the
source: the strip's three steps and their channel counts are decided before the compiler
runs. The graph finds out later only because it asks later.

## What catenae does instead

In catenae the strip is a constant, and its type is its topology.

```cpp
{{#include ../../examples/strip.h:strip}}
```

Each operand of `|` is a *stage*: a small constant that says what one step is configured
with. `gain<2>` is a stereo gain and `delay<2, 32>` is a stereo delay of 32 frames. The
channel count is a template argument, not a number reported on request. A stage declares
what it reads and what it writes as two types, its input *port* and its output port. The
members a stage declares are listed on
[the stage contract's page](api/stage_contract.html).

`a | b | c` builds a *chain*, and the chain's type names every stage in order. When the
compiler forms that type it forms each *joint*: one stage's output port set against the
next stage's input port. Here every joint is a `port<float, 2>` meeting a `port<float, 2>`.

### A joint that does not hold

Write a stereo gain into a mono delay:

```cpp
constexpr auto wrong = catenae::gain<2>{1.0f, 96} | catenae::delay<1, 32>{};
```

This is the first error GCC 13 prints:

```text
error: static assertion failed: catenae: one stage's output and the next stage's input have different channel counts
```

There is no image, so there is no device on which this can go wrong, and no `false` for
anyone to handle.

The message is fixed text. Two ports can differ in more ways than their channel counts:
in sample format, in layout (interleaved or planar), in kind (a time-domain signal or a
spectrum), in the number of bins of a spectrum. Each way has its own message. Each message
is pinned by a case under `tests/compile_fail/`: a source file the build must fail to
compile, with that text in the error. The case for this one is `joint_channels.cpp`, and
it runs on every CI leg.

### What else the type proves

The joints are not the only facts settled when the type is formed. A chain composes its
stages' numbers into its own, and they can be asserted before the program exists.

```cpp
{{#include ../../examples/strip.h:proven}}
```

The first says the strip is frame-synchronous: n frames in, n frames out, on every call.
The second says it is in place: each stage may write over its input, so the chain may, and
the output buffer can be the input buffer. The third says the strip delays every
frequency by exactly 32 frames. That is the delay's 32 and nothing from the other two.

A chain is itself a stage, so a chain can be an operand of `|` like any other. A stage
from a namespace of your own joins through `catenae::chain(a) | b`.
[Writing a stage](writing_a_stage.md) explains why.

### The caller's buffers

The graph's `process` trusted a pointer and a count. The chain's `process` is where a
caller's buffers enter, and it is the one place where they are checked. When the blocks
have sizes the compiler can see, the compiler does the checking.

```cpp
{{#include ../../examples/stages_and_chains.cpp:callback}}
```

`memory_of<strip_t>` is the storage for one instance: what the strip remembers, and what
one call needs. The program resets the state once before the first callback. The callback
takes 64 stereo frames and returns 64.

Make the output one frame short, `std::array<float, 63 * 2>& out`, and GCC 13 prints:

```text
error: static assertion failed: catenae: a fixed output block holds max_outputs_for of the input block
```

`max_outputs_for` is the most frames a call can write for a given input. For this strip
it is the input's own count, so 64 frames in need room for 64 out. This message is pinned
as well, by the case `tests/compile_fail/fixed_block_output_short.cpp`.

Blocks whose sizes are not known until run time are checked when they enter `process`, in
every build. The stages' own `run` functions check nothing.

### What was given up

The graph can be rebuilt while the program runs. A chain cannot: its topology is its type,
and changing it means compiling again.

In return, a strip that does not join is not a state the program can be in. The
composition rules, and the requirements that pin them, are on
[the chain contract's page](api/chain_contract.html).
