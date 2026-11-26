# Assumptions of use {#assumptions}

<!-- Copyright 2026 Timothy Place and the catenae contributors -->

What catenae requires of its integrator and cannot check, or checks only at the `audit`
level (PLAN.md section 8). An identifier is never reused.

| Assumption | What the integrator ensures | Checked |
|---|---|---|
| AOU-001 | One thread per side of a junction or a mailbox. | no |
| AOU-002 | A state is reset before its first `process`, and afterwards only between calls, on the thread that runs the chain. | at audit, `check::state_not_reset` (from C1) |
| AOU-003 | A workspace is used by one thread at a time. | no |
| AOU-004 | A posted value owns what it refers to. | where its type says it holds a view (`holds_view`): a mailbox of it does not compile (CAT-MAILBOX-004) |
| AOU-005 | A junction's configuration does not change while either side runs. | no |
| AOU-006 | A caller that pushes and pulls a `handoff` by hand in fixed blocks has checked `serves()` for them; `fed_by` asserts it where it makes the pull. | where catenae makes the pull |
| AOU-007 | `in` and `out` do not overlap; a stage that `is_in_place` may also be given one buffer as both. | at audit, in `gain`, whose loops rely on it (`check::buffers_overlap`) |
| AOU-008 | The host's flush-to-zero setting is the one the build states (`CATENAE_FLUSH_TO_ZERO`, on by default). | at audit, by every recursive stage's `run`, `check::flush_to_zero` |
| AOU-009 | `check_failed` does not return. | `[[noreturn]]`: the compiler warns at a return |
| AOU-010 | A setter is called between calls, never during one. | no |
| AOU-011 | The amalgamation is built with floating-point contraction off. | no |
| AOU-012 | The amalgamation is built with the compiler's constant-evaluation limit raised for the FFT's tables where its default is short: Clang `-fconstexpr-steps=2000000000`, MSVC `/constexpr:steps2000000000` (GCC's defaults suffice). | the compiler: a table it cannot finish does not compile |
| AOU-013 | The storage a run-time FFT plan is made into outlives the plan and is not written while the plan is in use. | no |
