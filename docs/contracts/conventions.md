# Cards and requirements {#conventions}

<!-- Copyright 2026 Timothy Place and the catenae contributors -->

How a contract is written down (PLAN.md section 11.2, rules 1 to 3).

## The card

A public header opens with a card: one `@file` and `@brief` line, then a table of fixed
fields, then its requirements. A stage's card has these fields, in this order, each one line:

| Field | What it says |
|---|---|
| Ports | the port types in and out |
| Ratio | L/M, in place or not, the input group, the shortfall and lead |
| Configured | by what design, and which setters run between calls |
| State | what it holds, and how it rests (`rests_at_zero` or `reset`) |
| Workspace | what a call needs, or none |
| Latency | what `nominal` and `deviation` measure |
| Tail | `tail_frames()` and the residue it holds to, over which designs |
| Formats | the sample formats it is instantiated for |
| Limits | each known failure mode, pinned by a test |
| Method | the published method, cited |
| Rationale | the decisions it rests on, by number |

A header of the vocabulary (a span, a ratio, the traits) has a card of the same shape with
the fields its contract needs. A card states the present: what the code used to do is the
changelog's, and why it is so is the plan's, by decision number. A run of `///` lines is at
most 60 lines (`scripts/check_budgets.py`).

## Requirements

A requirement is one promise with one identifier, `CAT-<AREA>-<nnn>`, written
`@req{CAT-<AREA>-<nnn>}` in a `///` comment of the header, or on the contract's page when the
promise is the contract's (`docs/contracts/`). It names its classes of input and its boundary
values: "512 compiles, 516 does not", not "at most about 512". Doxygen collects every one onto
the requirements page. An identifier is never reused: a promise withdrawn leaves a gap.

A test cites the requirements it pins on the comment line directly above it,
`// Verifies CAT-SPAN-004.`; a compile-fail case (`tests/compile_fail/`) or a link probe
(`tests/link/`) cites them in its opening comment. `scripts/check_requirements.py` fails the
build when an identifier is declared twice, when one is cited by nothing, when a test cites
none, or when a citation names an identifier nothing declares.

## Other identifiers

`AOU-<nnn>` is an assumption of use: what the library requires of its integrator and cannot
check, or checks only at the audit level (@ref assumptions). `DEV-<nnn>` is a deviation: what
knowingly departs from the rules the library is written to (@ref deviations). A suppression
of a warning cites one, and `scripts/check_suppressions.py` counts the citations against it.
