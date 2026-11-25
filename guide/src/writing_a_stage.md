<!-- Copyright 2026 Timothy Place and the catenae contributors -->

# Writing a stage

A stage of your own is a class that declares a fixed set of members, carries a card at the
top of its header, and passes the same four generic tests the library's stages pass. The
compiler checks the first, a reader checks the second, and the battery checks the third.

This chapter writes one stage: the first difference, `y[n] = x[n] - x[n - 1]`, per
channel. It lives in a namespace of its own, `example`, as yours will. Each member it
declares is documented once, on [the stage contract's page](api/stage_contract.html).
This chapter says why each group of members exists.

## The contract

Here is the whole stage.

```cpp
{{#include ../../examples/difference.h:stage}}
```

The class is the stage's *configuration*: a small constant that can be copied freely. This
one has no data members, because a first difference has nothing to configure. What an
instance remembers is not in the class. It is in `state`. The members fall into five
groups.

**The ports and the arrival numbers.** `input_port` and `output_port` say what the stage
reads and writes: here `float` samples, `Ch` channels to a frame. `rate_ratio` says how
many frames come out for the frames that go in. The next four members say how the output
arrives. A stage that works in blocks writes nothing until a block is full, so its output
trails its input and then catches up. `input_group`, `max_shortfall`, `max_lead` and
`output_quantum` publish that pattern as numbers a chain can compose when it is compiled.
The first difference keeps pace frame by frame: a group of 1, no shortfall, no lead. `is_in_place` says the output buffer may be
the input buffer. That is why `run` reads each sample before it writes one.

**Latency.** `has_exact_latency` says every frequency is delayed alike, and `exact_latency` says by how
much, as a ratio of output frames. Here it is half a frame. The run-time `latency()`
carries the same fact as a nominal value and a deviation from it, for the stages whose
delay varies with frequency.

**The state and how it rests.** `state` is everything one instance remembers: here the
last frame. It is plain storage, with no constructor and no default member initialiser, so
that a static one costs no start-up code. Storage that is all zero means "not yet reset".
The stage says how its state comes to rest, in exactly one of two ways. A stage whose rest
is all zero declares `rests_at_zero`, as this one does, and is zeroed in place. A stage
whose rest depends on its configuration writes a `reset` member instead. `workspace` is
what one call needs and nothing keeps. This stage needs none and says so with
`no_workspace`.

**The work and the counts.** `run` does the arithmetic. It is trusted: it consumes all of
its input, returns the frames it wrote, checks nothing, and cannot fail. A caller's
buffers are checked once, where they enter the chain. The three count functions are how the chain knows what `run` will do before it calls it.
`outputs_for` is exactly what the next call of n frames writes. `frames_needed` is the
smallest input that writes k frames. `max_outputs_for` is a bound from any state. For a
stage that keeps pace all three are the identity. For a decimator or a block stage the
first two depend on the state, which is why they take it.

**The tails.** When the input ends, a stage with memory still has output to give.
`tail_frames()` is the number of zero input frames after which the output is silence
within `flush_residue()`. The first difference remembers one frame, so its tail is one
frame, and a finite impulse response leaves no residue. `practical_tail_frames()` is the
frames to -120 dB re full scale.

### What the compiler does for you

`catenae::stage_traits` reads every one of these members. A member that is missing, or of
the wrong type, fails to compile with one `static_assert` whose message names it. So do the
rules between members: a state with a constructor, a stage that declares both ways to rest
or neither, an in-place stage whose counts do not allow it.

The traits check a class's shape. They do not make it a stage. A class becomes one when
its author says so, by specialising `catenae::is_stage`:

```cpp
{{#include ../../examples/difference.h:opt_in}}
```

### Joining a chain

With that, the stage joins a chain beside the library's own.

```cpp
{{#include ../../examples/writing_a_stage.cpp:chain}}
```

The first thing to see is `catenae::chain(...)`. The `|` that builds a chain is declared
in the `catenae` namespace, and the compiler finds an operator through the namespaces of
its operands. So it finds catenae's `|` when at least one operand is a catenae type. Two
stages of your own namespace side by side find nothing, and the expression does not
compile. Wrapping the first stage in `catenae::chain()` makes the left operand the
library's, a chain of one, and every `|` after it then has a chain on its left. Writing
`using catenae::operator|;` in your own scope also works.

The second is the `static_assert`. A chain's joints are formed when its type is formed.
Each stage's output port is matched against the next stage's input port, and a mismatch
fails to compile with a message that says how the two ports differ. The composed numbers
are formed at the same moment, which is why the chain's `exact_latency` can be asserted
before the program runs: the difference's half frame, and nothing from the gain. The
details are on [the chain contract's page](api/chain_contract.html).

## The card

The compiler reads the members. A person reads the card: a table of fixed fields at the
top of the header, always in the same order, each one line.

```cpp
{{#include ../../examples/difference.h:card}}
```

The fields follow the groups above: the ports and the ratio, what configures the stage,
its state and workspace, latency and tail. Four more say what the members cannot.
`Formats` lists the sample formats the stage is written for. `Method` cites the published
method. `Limits` lists each known failure mode, and each is pinned by a test. The first
difference has two. At the Nyquist frequency it doubles the signal, so a full-scale input
comes out at twice full scale. A NaN or an infinity spoils its own frame and the next.
`Rationale` names the decisions the stage rests on, by number. A card states the present.
[The conventions page](api/conventions.html) gives the full list of fields and their
order.

Below a library stage's card come its requirements. A requirement is one promise with one
identifier, written `@req{CAT-<AREA>-<nnn>}`, and it names its boundary values rather than
approximating them. Each of the library's tests cites the requirements it pins on the
comment line directly above it. A script reads the library's tests and fails the build
when a requirement is cited by no test, or a test cites none.

The example declares no requirements of its own and, being outside the library, is not
read by that script. Its generic test cites the stage contract's `CAT-STAGE-009` to `012`,
the promises those four functions check. The tests after it are named for the promise on
the card they pin, as every test is: `PeaksAtTwiceItsInputAtNyquist`,
`NonFiniteInputSpoilsTwoFrames`.

## The battery

The compiler proves the members exist and have the right types. It cannot prove they are
true. That is what the battery is for.

```cpp
{{#include ../../examples/difference_battery.cpp:battery}}
```

The first test runs the four generic tests. Each takes the stage's configuration, drives
it through its own `run` with a deterministic pseudo-random signal, and returns a result
that names what broke.

- **`reset_ignores_storage`** fills the state with garbage, resets it, and compares the
  output bit for bit with a state reset over zero. It uses three garbage patterns, because
  one pattern can hide a forgotten member. It catches a `reset` that misses a field.
- **`counts_are_exact`** checks, before each call, that `outputs_for` is what `run` then
  writes, that `frames_needed` is the smallest input that writes k frames, and that no
  call exceeds `max_outputs_for`. It catches a count function that has drifted from the
  work.
- **`arrival_numbers_hold`** finds the arrival numbers by counting. It feeds the stage
  from reset, counts what comes out, and compares the result with `input_group`,
  `max_shortfall`, `max_lead` and `output_quantum`. With its second argument true, as
  here, the published numbers must be the tightest true values, not only bounds that hold.
- **`chunking_is_bit_exact`** feeds one input in a single call and again in several random
  divisions, calls of zero frames included, and requires bit-identical output. It catches
  a stage whose result depends on how its caller's blocks happen to fall.

These are the tests every stage and chain in the library runs. A stage of your own calls
the same functions, and so is held to the same contract.

The generic tests know nothing about what a stage computes. A stage that wrote silence
could pass all four. So the battery ends with the stage's arithmetic and its two limits,
each a known input and a known output. This is the part only you can write.
