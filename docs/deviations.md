# Deviation log {#deviations}

<!-- Copyright 2026 Timothy Place and the catenae contributors -->

What knowingly departs from the rules catenae is written to (PLAN.md section 8, D31). The
opening entries are from public summaries of MISRA C++:2023 and are unverified against its
text; no rule is quoted or numbered. An identifier is never reused.

"Uses" counts the suppressions of a warning that cite the entry, and
`scripts/check_suppressions.py` fails the build when the count differs; "—" marks a construct
rather than a suppression, which no suppression may cite. Where a construct is used is in the
last column, and in a comment citing the entry where it is.

| Entry | Departure | Why | Uses | Where |
|---|---|---|---|---|
| DEV-001 | A compiler extension: `CATENAE_CONSTINIT` | C++17 has no `constinit`; GCC's and Clang's spellings make a static that start-up code would build an error | — | `catenae/compiler.h` |
| DEV-002 | A compiler extension: inlining control | kernels and checks inline where they are called, cold paths stay out of line | — | `catenae/compiler.h` |
| DEV-003 | A compiler extension: `restrict` | a kernel's pointers do not alias, and the compiler is told so | — | `catenae/compiler.h` |
| DEV-004 | A compiler extension: atomic operations on a plain word | a `std::atomic` member would give a junction's state a constructor from C++20 on | — | `catenae/compiler.h` |
| DEV-005 | A compiler extension: reading the floating-point control register | the audit check of the host's flush-to-zero setting | — | from C3 |
| DEV-006 | `memcpy` bit casts, and the `memset` behind `rests_at_zero` | C++17 has no `std::bit_cast`; zeroing a plain state in place builds no temporary | — | `catenae/stage_traits.h` |
| DEV-007 | A shift of a signed value | the fixed-point formats scale by shifting; one helper does every such shift, and shifts only values that are not negative, so none is implementation-defined | — | `catenae/detail/shift.h` |
| DEV-008 | Guard-clause early returns, against a single exit point | a precondition handled first reads more plainly than a nested body | — | where a function has one |
| DEV-009 | `-Wunsafe-buffer-usage` off | a kernel indexes spans in its inner loop; the audit level checks the indices | — | `tools/warnings/clang.txt` |
| DEV-010 | vDSP's allocation | Accelerate's FFT setup allocates; it is made in a design's `make()`, outside the audio path | — | from C4 |
| DEV-011 | Static storage for states, configurations, workspaces and mailboxes | no heap (D22); a caller's arena is the alternative the guide shows | — | an integrator's |
| DEV-012 | Implicit conversions of `span` and `expected` | the standard's interface has them | — | `catenae/span.h`, `catenae/expected.h` |
| DEV-013 | Placement new and `std::launder` in the mailbox | a post constructs the value in its slot, a take reads it through the laundered pointer | — | `catenae/mailbox.h`; `scripts/check_slots.py` refuses an assignment into a slot (CAT-MAILBOX-003) |
| DEV-014 | Two members `stage_traits` looks for | to insist that a stage declares exactly one way to rest, it detects both `reset` and `rests_at_zero` | — | `catenae/stage_traits.h` |
| DEV-015 | MSVC's C4611 off around the battery's one `setjmp` | a failed check reaches a `[[noreturn]]` handler, and only `setjmp` and `longjmp` bring it back to the test that expected it on every leg without exceptions (D21); MSVC warns at any `setjmp` in C++, and this one's frame holds nothing to destroy | 1 | `tests/support/catenae_test/check_trap.cpp` |
