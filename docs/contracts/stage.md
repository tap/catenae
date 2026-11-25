# The stage contract {#stage_contract}

<!-- Copyright 2026 Timothy Place and the catenae contributors -->

A stage is a configuration: a small, trivially copyable constant (D34). What one instance
remembers is its `state`, and what one call needs is its `workspace`. `stage_traits<T>`
(`catenae/stage_traits.h`) reads the members below and fails with one `static_assert` for
each that is missing or ill-typed; a stage opts in through `is_stage<T>`. The members are
documented here, once, and nowhere else (PLAN.md section 11.2, rule 2). The words are the
@ref glossary "glossary"'s.

| Member | Meaning |
|---|---|
| `input_port`, `output_port` | The port types it reads and writes: `port`, `spectrum` or `wire_port`. |
| `rate_ratio` | `static constexpr ratio`: L/M, reduced, neither term 0. |
| `state` | Everything one instance remembers. Plain storage: trivially copyable, standard-layout, trivially default constructible, an aggregate. All zero is "not yet reset" (D49). |
| `workspace` | What one call needs and nothing keeps; `no_workspace` for none (D50). Plain storage, never reset. |
| `reset(state&) const noexcept` or `rests_at_zero = true` | How the state comes to rest: exactly one. A stage whose rest is all zero declares `rests_at_zero` and is zeroed in place; one whose rest depends on its configuration writes `reset`. |
| `input_group` | `static constexpr std::size_t`: with N frames in since reset and O(N) out, the smallest n with O(kn) = kn L/M for every k. 1 for a stage that keeps pace frame by frame, M for a decimator, the hop for a block stage. |
| `max_shortfall`, `max_lead` | `static constexpr ratio`: the most output frames by which O(N) trails N L/M, and runs ahead of it, over every N from reset. |
| `output_quantum` | `static constexpr std::size_t`: every call's output count is a multiple of it. |
| `is_in_place` | `static constexpr bool`: whether `in.data() == out.data()` is allowed. Only with one port type, `input_group` 1 and `max_outputs_for(n) == n`. |
| `has_exact_latency`, `exact_latency` | Whether every frequency is delayed alike, and if so by how much: a reduced ratio of output frames (D39). |
| `run(state&, workspace&, in, out) const noexcept` | The work, trusted: consumes all of `in`, returns the frames written to `out`. No allocation, no failure, no boundary check. |
| `outputs_for(const state&, n)` | Exactly what the next call of n frames writes. |
| `frames_needed(const state&, k)` | The smallest n with `outputs_for(n) >= k`. |
| `max_outputs_for(n)` | `static constexpr`: a bound on any call of n frames from any state, non-decreasing in n. |
| `latency()` | `stage_latency{nominal, deviation}` in output frames. |
| `tail_frames()`, `practical_tail_frames()`, `flush_residue()` | Zero input frames after which the output is silence within the residue, and the frames to -120 dB re full scale. |

`stage_traits<T>` derives `is_frame_synchronous`, which is `input_group == 1`: every call of
n frames writes exactly n L/M. A stage never declares it (D35); a chain carries its own,
derived the same way from its composed `input_group`.

**The law.** When frames reach a stage in whole multiples of q, it trails or leads by at most
its `max_shortfall` or `max_lead` scaled by (group - gcd(q, group)) / (group - 1), and by
nothing when q is a whole number of groups (PLAN.md section 3.2).

## Requirements

@req{CAT-STAGE-001} `stage_traits<T>` reads each member of the table, and a member that is
missing or of the wrong type fails to compile with one `static_assert` whose message names it.

@req{CAT-STAGE-002} A state and a workspace are plain storage; one with a constructor or a
default member initialiser fails to compile.

@req{CAT-STAGE-003} A stage declares exactly one way to rest: neither, both, or
`rests_at_zero = false` fails to compile. `stage_traits<T>::reset` zeroes the state of one
that rests at zero and calls the other's `reset`, whatever the storage held.

@req{CAT-STAGE-004} `is_frame_synchronous` is `input_group == 1`; a stage that declares it
fails to compile, unless it is a chain, which derives its own from its composed group.

@req{CAT-STAGE-005} A stage is in place only with one port type, `input_group` 1 and
`max_outputs_for(n) == n` (read at fourteen values of n from 0 to 4096); any other in-place
stage fails to compile.

@req{CAT-STAGE-006} `rate_ratio` is reduced and neither term is 0, and `input_group` and
`output_quantum` are at least 1; any other value fails to compile.

@req{CAT-STAGE-007} A configuration is trivially copyable and, unless `is_chain<T>` says it is
a chain, at most `max_configuration_bytes` (512) bytes: 512 compiles, 516 does not (D54).

@req{CAT-STAGE-008} A stage opts in through `is_stage<T>`, false unless its author
specialises it; `stage_traits<T>::opted_in` reads it.

@req{CAT-STAGE-009} The counts are true of `run`: from every state a call reaches,
`outputs_for(n)` is what a call of n frames writes, `frames_needed(k)` is the smallest such n,
and no call writes more than `max_outputs_for(n)`.

@req{CAT-STAGE-010} The arrival numbers are true of `run`: counting from reset, the smallest
n that keeps pace is `input_group`, the output never trails or leads by more than
`max_shortfall` and `max_lead` and, fed frame by frame, reaches each of them; every call's
count is a multiple of `output_quantum`; and the law holds for every divisor of the group.

@req{CAT-STAGE-011} A state reset over any bytes (0xA5, 0x5A, 0xFF) and one reset over zero
give bit-identical output (D49).

@req{CAT-STAGE-012} Any division of one input into calls, sizes 0 included, gives
bit-identical output.

The battery checks CAT-STAGE-009 to 012 with its generic tests
(`tests/support/catenae_test/harness.h`), which every stage and chain runs. Each test proves
itself on the fixture stages and on a stage broken in the one way it exists to find
(`tests/test_harness.cpp`).
