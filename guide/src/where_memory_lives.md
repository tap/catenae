<!-- Copyright 2026 Timothy Place and the catenae contributors -->

# Where memory lives

On a desktop, memory is something you ask for. On a microcontroller it is a map, drawn
when the program is linked. The program sits in flash, which holds what does not change:
the code, and the constants (`.rodata`). RAM holds what changes, in three places the
linker gives a size to. `.data` is the variables that start with a value; the value is
kept in flash and copied to RAM before `main`. `.bss` is the variables that start as zero;
it costs RAM and nothing in flash. The third is the stack. A heap, if there is one, is a
fourth region, and how full it gets is decided as the program runs.

This chapter's constraint is that map: flash, RAM, `.data` and `.bss`, and the stack. It
explains why catenae has no heap.

The example is one stereo strip: a level that ramps over 96 frames when it changes, a
delay of 32 frames, and a step that halves what comes in.

## What you would naturally write

You would write a class. It holds what the strip is set to and what the strip remembers,
together. The delay line is a `std::vector`, sized by the constructor. The program has one
strip, so it has one global instance.

```cpp
{{#include ../../bench/probe/strip_object_probe.cpp:object}}
```

This is ordinary, correct C++. `process` allocates nothing: the one allocation happens
once, in the constructor, before `main`.

## What it costs on the target

The harness builds this object into an image of its own (`strip_object`), and the same
strip written catenae's way into another (`strip`). The arithmetic is the same. The
target is the Cortex-M33, built for size (MinSizeRel) with arm-none-eabi-gcc 13.2.1. All
figures are bytes, except the count of start-up functions.

| Figure | `strip_object` | `strip` |
|---|--:|--:|
| The whole image's `.text`, runtime included | 18,100 | 7,712 |
| The probe's own `.bss` | 32 | 804 |
| The probe's own `.data` | 0 | 0 |
| Start-up functions the probe's own code adds | 1 | 0 |
| What the heap holds after start-up | 1,360 | not measured |
| The start-up's worst-case stack, static | no static bound | 12 |
| `process`: worst-case stack, static / measured | 16 / 16 | 32 / 32 |
| `process`: code it can reach | 136 | 320 |
| `reset`: stack / code it can reach | none: the constructor | 32 / 218 |

The numbers are from
[bench run 45704804502](https://github.com/tap/catenae/actions/runs/45704804502). They are
recorded in `bench/ceilings.json`, and this reproduces them:

```sh
cmake -S . -B build-size -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m33-mps2.cmake \
      -DCATENAE_BUILD_TESTS=OFF -DCATENAE_BUILD_EXAMPLES=OFF -DCATENAE_BUILD_BENCH=ON
cmake --build build-size
scripts/ceilings.py measure --key m33 --build-dir build-size
```

A start-up function is one the runtime calls before `main`. The strip's 12 bytes of
start-up stack are the C library's own (newlib's `register_fini`), not catenae's. The 320
bytes that the strip's `process` can reach include `memcmp`, and the 218 of its `reset`
include `memset`.

Read the first column against the map.

- **The map does not show the memory.** The object's `.bss` is 32 bytes. After start-up
  the heap holds 1,360 bytes, and no section of the image shows them. 256 of those are
  the delay line: 32 frames of 2 channels of 4 bytes.
- **Code runs before `main`.** The image has three start-up functions: newlib's
  `register_fini`, the object's constructor, and a static initialiser from libstdc++'s
  exception allocator (`eh_alloc.o`). The strip's image has one, `register_fini`.
- **The exception machinery came too.** The build is `-fno-exceptions`, but `operator
  new` brings the machinery with it. `__cxa_throw` and the unwinder
  (`_Unwind_RaiseException`) are in the image.
- **The start-up's stack has no static bound.** `operator new` calls the new-handler
  through a pointer, and a call through a pointer cannot be followed in the image.
  `scripts/ceilings.py` says so for this probe.
- **The image is 10,388 bytes larger.**

The second and third are visible in the image itself:

```sh
arm-none-eabi-objdump -s -j .init_array build-size/bench/catenae_probe_strip_object
arm-none-eabi-nm build-size/bench/catenae_probe_strip_object | grep -e __cxa_throw -e _Unwind_RaiseException
```

The first command prints three entries. Run on `catenae_probe_strip`, it prints one.

Two things in this table do not favour catenae, and they are part of the measurement.

The object's `process` is the smaller one: 136 bytes of code and 16 of stack, against 320
and 32 for the chain. What the heap object costs is at start-up and in the map, not in
its loop.

And neither image is free of an allocator. The test platform's C library, newlib with
semihosting, links its own into both images for its stdio: the map shows `_malloc_r`
pulled in by newlib's `findfp.o`. The claim is narrower than "no `malloc` in the image".
catenae's code uses no allocator, adds no start-up function and adds no `.data`.

## What catenae does instead

catenae takes the object apart into three things, and gives each a place on the map.

The first is the *configuration*: what the strip is set to. It is a constant.

```cpp
{{#include ../../examples/strip.h:strip}}
```

Each operand of `|` is a *stage*, a small constant that says what one step is set to. `|`
joins them into a *chain*, whose type names its stages in order.
[Stages and chains](stages_and_chains.md), the next chapter, builds this strip and shows
what that type proves. For this chapter one fact is enough: the constant holds settings
and no memory. It costs no RAM. Its 12 bytes of `.rodata`, and the code, are in flash.

The other two are what the object kept in its members and on the heap.

```cpp
{{#include ../../bench/probe/strip_probe.cpp:memory}}
```

`memory_of<strip_t>` is the storage for one instance. It is a plain aggregate with no
constructor, so a static one is in `.bss`. `CATENAE_CONSTINIT` makes GCC and Clang refuse
a static that would need start-up code. The memory has two parts.

- The *state* is what the strip remembers between calls: the delay line and the ramp. It
  is 288 bytes.
- The *workspace* is what one call needs and no instance keeps. It is 516 bytes. Every
  state of the same chain type can share one workspace, so a program needs one per
  thread. It is never reset.

Together they are the 804 bytes of `.bss` in the table (`sizeof.state` and
`sizeof.workspace`, from the same run). There is no `.data`, and no start-up function.
The memory that the object's map hid is memory this map shows: what the strip needs in
RAM is a number in the link map, known before the image runs.

### Reset

A constructor did two jobs: it found the memory, and it gave the memory its first values.
The map has done the first. The second is `reset`.

In catenae, storage that is all zero means "not yet reset". It does not mean "at rest".
The configuration resets the state, in place, before the first call. Here it sets the
gain's level, which is 1.0 and not zero. The cost is in the table: 218 bytes of code and
32 of stack, in a call the program makes when it chooses and whose stack has a bound.

A stage says how its state comes to rest in exactly one of two ways. It writes `reset`, or
it declares `rests_at_zero = true` and its state is zeroed in place. A chain's reset calls
each stage's on that stage's part of the state.
[The stage contract's page](api/stage_contract.html) lists both, and
[the chain contract's page](api/chain_contract.html) defines `memory_of`.

### The rule in general

The strip shows two of the four places catenae keeps memory without a heap.

| Pattern | For |
|---|---|
| A state sized by template arguments | Histories, FIFOs, holdovers: what one instance remembers |
| A workspace sized by template arguments | Scratch between stages, staging, a transform's buffer: what one call needs |
| A constant viewed, not copied | Taps, tables, designs: in `.rodata`, and the stage holds a view |
| Storage the caller supplies | A size known only at run time |

### What was given up

A size chosen at run time needs a maximum chosen at compile time, or storage the caller
supplies. A state or a workspace is as large as its buffers, so it belongs in static
storage or a caller's arena, never on a small thread's stack. A chain has three names
(its configuration, its state and its workspace), or two with `memory_of` when there is
one instance. And a state is reset before it is used.

The same figures for the other Cortex-M cores are in the generated table of
`bench/README.md`. `.bss` and `.data` are the same on all four. The stack and the code
vary a little by core.
