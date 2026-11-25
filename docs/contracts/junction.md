# The junction contract {#junction_contract}

<!-- Copyright 2026 Timothy Place and the catenae contributors -->

A junction is where two call sites meet: one thread pushes, one other thread pulls (D19). Like
a stage it is a configuration and a state. `junction_traits<T>`
(`catenae/junction_traits.h`) reads the members below and fails with one `static_assert` for
each that is missing or ill-typed; a junction opts in through `is_junction<T>`. The members
are documented here, once.

| Member | Meaning |
|---|---|
| `sink_port`, `source_port` | The port types `push` takes and `pull` fills. |
| `clocking` | `static constexpr clocks`: `shared` (two threads, one clock) or `independent`. |
| `rate_ratio` | `static constexpr ratio`: the nominal ratio between the sides. |
| `state` | Plain storage, at rest when all zero. The words the two sides share are plain 32-bit words under the atomic operations of `catenae/compiler.h`: a `std::atomic` member would give the state a constructor from C++20 on. Not copied while either side runs. |
| `tolerance_ppm()` | Independent clocks only: the offset and drift it absorbs. |
| `push(state&, in) const noexcept` | Exactly one thread: the frames accepted. Wait-free. |
| `pull(state&, out) const noexcept` | Exactly one other thread: always fills `out`, silence where it could not supply, and returns how many frames were real. At most one pull per consumer period (D52). |
| `serves(largest_pull, quantum)` | `static constexpr bool`: whether pulls of up to `largest_pull` frames are served without running out, where `quantum` divides every push and pull (1 when their sizes vary). `fed_by` asserts it. |
| `report(const state&) const` | Any thread: the junction's own report type, each field one atomic 32-bit word, with no snapshot across fields. |
| `latency(const state&) const` | `junction_latency{nominal, range}` in source frames: a junction's delay breathes with its fill. |
| `capacity_frames` | `static constexpr std::size_t`, a power of two. |
| `reset(state&) const noexcept` | Empties it, while neither side runs. A static state needs none before first use. |
| `restart(state&) const noexcept` | The consumer's thread, while the producer runs: discards what is buffered and fills again. |

## Requirements

@req{CAT-JUNCTION-001} `junction_traits<T>` reads each member of the table, and a member that
is missing or of the wrong type fails to compile with one `static_assert` whose message names it.

@req{CAT-JUNCTION-002} A junction's state is plain storage, at rest when all zero, so a static
one is ready as it stands; one with a default member initialiser fails to compile.

@req{CAT-JUNCTION-003} A junction of independent clocks declares `float tolerance_ppm() const`,
and one without it fails to compile; one of shared clocks need not.

@req{CAT-JUNCTION-004} `capacity_frames` is a power of two (8 compiles, 12 does not), and
`junction_traits<T>::serves` is the junction's own, a constant expression.

@req{CAT-JUNCTION-005} `push` and `pull` are `noexcept` and return the frames they took and
gave; `reset` and `restart` are `noexcept` and return nothing.
