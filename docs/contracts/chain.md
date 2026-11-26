# The chain contract {#chain_contract}

<!-- Copyright 2026 Timothy Place and the catenae contributors -->

A chain is a type the caller writes (D13): `a | b | c` over configurations builds
`chain<A, B, C>` (`catenae/chain.h`). It holds its stages' configurations and nothing else,
so it is a configuration too, trivially copyable when they are and exempt from the 512-byte
limit (D54). It is itself a stage, so chains nest. Its memory is in two parts, as a stage's
is: a `state` per instance and a `workspace` per thread (D50); `memory_of<Chain>` holds one
of each.

| Member | Meaning |
|---|---|
| `a \| b` | Always flattens, whichever side a chain is on: `(a \| b) \| (c \| d)` is `chain<A, B, C, D>`. A stage of another namespace joins through `chain(a) \| b`, or with `using catenae::operator\|`. |
| `chain(s)`, `with_block<N>(c)` | A chain of one; a chain in blocks of N frames (64 by default). A `blocked_chain` is one stage to `\|`, never flattened, and keeps its block. |
| `C{}` | When every stage has a default configuration, the chain of them, `constexpr` when theirs are; otherwise a chain has no default. |
| `process(state, workspace, in, out)` | The caller's entry: the input is whole frames and the output holds `outputs_for(n)` (boundary checks, every build), then the work a block at a time. Fixed blocks (`std::array`, a built-in array, a span of static extent) are checked by the compiler instead: whole frames, whole input groups, room for `max_outputs_for`. |
| `run(state, workspace, in, out)` | The same work with no check: what an enclosing chain calls. |
| `reset(state)` | Each stage resets its own part and the state is ready (audit: `check::state_not_reset`). |
| `flush(state, workspace, out[, tail::exact])` | The end of the stream, resumable: zeros until `out` cannot take another burst or the tail is fed. Finished when `flush_remaining(state)` is 0. |
| `stage<I>()`, `stage_state<I>(state)` | One stage's configuration, and its part of a state, by position. |
| The arrival numbers, latency, tails | Composed from the stages' (PLAN.md section 4). |

## Requirements

@req{CAT-CHAIN-001} `|` flattens: chains on either side contribute their stages, a
`blocked_chain` is one stage, and `chain(s)` of a chain is that chain. Each member is a stage
(opted in by `is_stage`) and never a junction, and each joint is formed when the chain's type
is, failing with the message for the way its two ports differ.

@req{CAT-CHAIN-002} `rate_ratio` is the reduced product of the stages'; `input_group` is the
smallest n for which the frames reaching every stage are whole groups of it (down by 2 then up
by 2 is 2, up then down is 1); `max_shortfall`, `max_lead` and `output_quantum` compose by the
quantum carried along the chain (191 for a 96-frame block before a 128-frame one, 959 for 480
before 512, 10 for 7 before 5), with a nested chain composed through.

@req{CAT-CHAIN-003} A chain passes the generic tests a stage does: reset over any bytes, the
counts, the arrival numbers by counting, and bit-identity under any chunking.

@req{CAT-CHAIN-004} `process` checks, in every build, that the input is whole frames
(`check::input_not_whole_frames`) and that the output holds `outputs_for(n)` frames
(`check::output_too_small`); at the audit level, that the state was reset
(`check::state_not_reset`). The trusted `run` checks nothing.

@req{CAT-CHAIN-005} A run of in-place stages shares one buffer: an in-place stage after the
first writes into the buffer its input is in, and a trailing run writes into the caller's
output; a chain of in-place stages is in place, and holds no buffer at all.

@req{CAT-CHAIN-006} A flush writes what zero-padding the input by the chain's tail would,
bit for bit, in calls that each fill as much of their output as one more burst would not
overflow; `flush_remaining` counts down to 0; a `process` after a partial flush carries on
as after that many zero frames, and `reset` clears a flush's progress.

@req{CAT-CHAIN-007} `stage<I>()` reaches stage I's configuration (a setter on it acts from the
next call) and `stage_state<I>(state)` its part of the state.

@req{CAT-CHAIN-008} `latency()` is each stage's nominal and deviation carried to the output
rate and summed; `has_exact_latency` holds when every stage's does, and `exact_latency` is
then the sum of each stage's carried the same way (3 frames, then a 1:2 stage of 1/2, then 5
frames: 23/2).

@req{CAT-CHAIN-009} `tail_frames()` is composed backward: each stage's own tail plus the most
input it needs, from any state, for the frames the stages after it need; after that many zero
frames the output of a chain of stages with no residue is silence.

@req{CAT-CHAIN-010} A `blocked_chain` works in blocks of its own length, sizes its buffers
from it, and writes what the same chain in its default block writes.

@req{CAT-CHAIN-011} A chain's state and workspace are plain storage, so `memory_of<Chain>`
written `CATENAE_CONSTINIT static memory_of<Chain> m{};` is in `.bss` with no start-up code.

@req{CAT-CHAIN-012} A planar port is carried through a chain as one span per channel, at the
caller's edges and between stages, a planar buffer holding its channels end to end.

@req{CAT-CHAIN-013} Over every chain of up to three of the count model's stages (decimators
writing on a group's first frame and on its last, interpolators, L/M stages, block stages),
the composed `max_shortfall` and `max_lead` are never below the worst trail and lead counted
frame by frame, `input_group` is a group by its definition, and the two block-stage pairs are
exact.

@req{CAT-CHAIN-014} A chain whose every stage has a default configuration has one, the chain
of those, and it is `constexpr` when theirs are; a chain with a stage that has none has none.
