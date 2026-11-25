<!-- Copyright 2026 Timothy Place and the catenae contributors -->

# Threads and clocks

A chain runs inside one call, on one thread. A product has more than one call site. A
control thread changes a level while the audio callback runs. One thread captures frames
and another plays them. On a microcontroller the second call site is often an interrupt
handler, which can run between any two instructions of the main loop.

This chapter's constraint is two call sites that cannot share a lock.

## What you would naturally write

Take the setting first. You would keep the settings in a struct, write them from the
control thread, and raise a flag. The audio thread looks at the flag at the top of its
callback and copies the struct when the flag is up. You would mark the flag `volatile`,
because another thread changes it.

```cpp
{{#include ../../examples/racy_level.cpp:racy}}
```

It reads well, and on a desktop it appears to work. The level changes when you move the
control, and nothing clicks on any day you test it.

The other natural answer is a mutex around the struct. That one is correct, and the
callback cannot use it. A thread that takes a lock waits whenever another thread holds
it, and the control thread can be stopped by the scheduler while it does. The callback
has a deadline on every block and must not wait for another thread. An interrupt handler
is worse off: the code it would wait for is the code it interrupted, which cannot run
again until the handler returns.

## What it costs on the target

The cost of the first version is not a time. It is a data race, and the measurement is
the thread sanitizer's verdict. The example's `main` starts a control thread that calls
`change` ten thousand times while the main thread calls `audio_callback` ten thousand
times. Build it under the sanitizer and run it. This run was built with Clang 18.

```sh
CXX=clang++-18 cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS=-fsanitize=thread -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread
cmake --build build-tsan
TSAN_OPTIONS=halt_on_error=1 build-tsan/examples/catenae_example_racy_level
```

```text
WARNING: ThreadSanitizer: data race
  Read of size 1 by main thread:
    #0 (anonymous namespace)::audio_callback() /home/user/catenae/examples/racy_level.cpp:32:13
    #1 main /home/user/catenae/examples/racy_level.cpp:48:9

  Previous write of size 1 by thread T1:
    #0 (anonymous namespace)::change(float, float) /home/user/catenae/examples/racy_level.cpp:26:26
    #1 main::$_0::operator()() const /home/user/catenae/examples/racy_level.cpp:44:13

  Location is global '(anonymous namespace)::s_changed' of size 1

SUMMARY: ThreadSanitizer: data race /home/user/catenae/examples/racy_level.cpp:32:13 in (anonymous namespace)::audio_callback()
```

These are the report's key lines. The process id, the addresses, the build IDs, the
`std::thread` frames under the write and the note of where the thread was created are
dropped. The paths are those of the machine that ran it.

The sanitizer stops at its first finding, and that finding is the flag itself. Line 32 is
`if (s_changed)` and line 26 is `s_changed = true`. `volatile` makes the compiler read
the flag every time. It orders nothing between two threads, and it is not atomic. In C++
a data race is undefined behavior, so the program has no meaning the compiler must keep.

Repairing the flag would not repair the design. The struct has two fields. The audio
thread can copy it while the control thread is between its two writes, and run with the
level of one change and the pan of another. The audio thread also lowers the flag after
its copy, so a change that lands between the copy and that line is lost.

The verdict is the same on every run, which is why it serves as the measurement. The
example never runs in the ordinary battery. On CI's thread-sanitizer legs a CTest test,
`example.racy_level_is_a_data_race`, builds it and passes only when the sanitizer reports
the race.

## What catenae does instead

Two threads meet in two places only: a *mailbox*, which carries one value, and a
*junction*, which carries frames. Each has exactly one thread on each side, and neither
side ever waits.

### A value: the mailbox

```cpp
{{#include ../../examples/threads_and_clocks.cpp:mailbox}}
```

The control thread posts and never touches the chain. The audio thread calls `take` at
the top of its callback. If a value has arrived since the last take, `take` calls the
function with it and returns true. Otherwise it returns false at once. The setter then
runs on the audio thread, between calls, which is the only place a stage's setter may
run.

Inside are three slots and one atomic word. The poster owns a slot and the taker owns a
slot. A post writes the whole value into the poster's slot, then swaps that slot with the
one in the middle. A take swaps the taker's slot with the middle one, then reads. Each
side touches only the slot it owns, so a value is never read while it is written. Two
fields posted together arrive together.

The newest value wins. A value that was never taken is replaced by the next post, so a
mailbox carries a state and not a queue of events. A level, a filter design and a meter
reading are states.

Three things follow from the mechanism.

- The value type is trivially copyable and trivially destructible. The mailbox asserts
  it.
- The value owns everything it refers to. A pointer inside it would hand the race back to
  you, and the compiler cannot see that one.
- A post constructs its value in place, with placement new, and never assigns into a
  slot. `scripts/check_slots.py` refuses an assignment.

The mailbox has a constant constructor and starts empty, so a static one sits in `.bss`
with no start-up code. The card and the requirements are on
[the mailbox's page](api/mailbox_8h.html).

### Frames: the junction

A chain never contains a junction, and there is no `|` across one. The thread boundary
stays visible in your code as two calls.

```cpp
{{#include ../../examples/threads_and_clocks.cpp:junction}}
```

`handoff<float, 2, 1024, 320, 64>` is a ring between two threads of one clock: stereo
`float`, room for 1024 frames, a target fill of 320, and pushes of at most 64. Its
configuration is its template arguments. Its state is plain storage that rests at zero,
so every object in the listing is static and built by constant initialisation.

The producer's thread calls `feeding(link, s_link, input, s_into_link, s_into_link_work,
in)`, which runs the chain and pushes what it wrote. The consumer's thread calls
`fed_by(link, s_link, output, s_from_link, s_from_link_work, out)`, which fills `out`
with exactly the frames asked for, in at most one pull on the junction.

Bursts and buffering ended with a queue whose two numbers were derived. A ring between
threads raises the same question, and `handoff` answers it with a fill policy stated as
numbers.

- A pull delivers silence until the ring holds the target. The pull that finds the target
  discards the oldest frames above it and starts paying out. So a stream that starts sees
  exactly the target, whichever thread began first.
- A pull the ring cannot fill is an underrun. It delivers what there is, pads with
  silence, and the ring fills to the target again.
- A push the ring cannot hold is an overrun. The newest frames are dropped.
- The nominal latency is the target. While the ring runs, the fill a pull sees stays
  within one push of it.

`fed_by` always returns the frames asked for, so an underrun shows as a count: `starved`,
in its state, is the number of input frames the junction supplied as silence.

The target is not a guess. For the ring never to run dry it must be at least the largest
pull plus the largest push, less one when their sizes vary. Here that is 256 + 64 - 1, or
319, and the example rounds it to 320. The pull side checks this when its type is formed.
With a target of 128 it does not compile:

```text
catenae: the junction does not serve this pull side: its target is below the largest pull plus its largest push, less one
```

The case `tests/compile_fail/fed_by_target_too_small.cpp` pins that refusal.

The cards are on the pages of [`handoff`](api/handoff_8h.html),
[`feeding`](api/feeding_8h.html) and [`fed_by`](api/fed__by_8h.html). What every junction
promises is on [the junction contract's page](api/junction_contract.html). `handoff` is
the junction for one clock. Two independent clocks need a junction that absorbs their
drift, the asynchronous converter of a later milestone.

### What is assumed, and how it is verified

Both rest on one assumption the library cannot check: one thread per side, where an
interrupt handler counts as a thread. It is AOU-001 in `docs/assumptions.md`. Two posters
on one mailbox are the race from the top of this chapter.

Inside that assumption the claims are tested where a mistake would show.

- `tests/test_mailbox.cpp` puts two million 64-byte values between two threads. None is
  torn and none is out of order.
- `tests/test_handoff.cpp` moves three million stereo frames between two threads, untorn
  and in order.
- Both run under the thread sanitizer on CI, and natively on an arm64 runner. An x86
  processor orders memory more strictly than an Arm one does, so a wrong memory order can
  pass there.
- On the bare-metal Cortex-M legs under QEMU, `tests/bare_metal/interrupt.cpp` makes a
  timer interrupt the mailbox's poster and the handoff's producer, against the main loop.
