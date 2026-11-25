# The analyzer contract {#analyzer_contract}

<!-- Copyright 2026 Timothy Place and the catenae contributors -->

An analyzer reads a signal and writes nothing: a meter, a pitch tracker (D11).
`tap<Ch, Analyzer>` is the stage that holds one and passes its input through. Its results
are read on the chain's thread and leave it through a mailbox. `analyzer_traits<T>`
(`catenae/analyzer_traits.h`) reads the members below and fails with one `static_assert` for
each that is missing or ill-typed; an analyzer opts in through `is_analyzer<T>`. The members
are documented here, once.

| Member | Meaning |
|---|---|
| `input_port` | What it reads. It has no output port. |
| `hop_frames`, `window_frames` | `static constexpr std::size_t`: a result every `hop_frames`, over the last `window_frames`. |
| `state` | As a stage's: plain storage. |
| `reset(state&) const noexcept` or `rests_at_zero = true` | As a stage's: exactly one. |
| `analyze(state&, in) const noexcept` | Trusted, as a stage's `run`. |
| `result_type`, `result(const state&) const` | The latest result: a value, or a view of storage in the state. |
| `results_ready(const state&) const` | `std::uint32_t`: results completed since reset, a running count that wraps. The reader keeps the last count it saw and takes the difference, so reading changes nothing. |

## Requirements

@req{CAT-ANALYZER-001} `analyzer_traits<T>` reads each member of the table, and a member that
is missing or of the wrong type fails to compile with one `static_assert` whose message names
it; an analyzer that declares an output port fails too.

@req{CAT-ANALYZER-002} An analyzer's state is plain storage with exactly one way to rest, as a
stage's.

@req{CAT-ANALYZER-003} `results_ready` counts each completed hop, one result each, and
`result` returns the latest as its `result_type`; neither changes the state.
