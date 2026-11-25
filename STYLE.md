# catenae house style

The Tap House Rules (D20), carried from DspTap and owned here: there is no drift check against
TapHouse. Where catenae's decisions differ from the Tap copy, the decision wins and is named
below. `.clang-format` checks layout, `.clang-tidy` checks names, braces and complexity, and the
scripts of `scripts/` check what neither can see (PLAN.md section 7.3). CI runs all three.

## 1. Naming

| Kind | Convention | Example |
|------|-----------|---------|
| Types (class/struct/enum/alias) | `snake_case` | `handoff`, `stage_traits` |
| Functions / methods | `snake_case` | `outputs_for`, `set_level` |
| Variables / parameters / locals | `snake_case` | `frame_count` |
| Template parameters | `PascalCase`, the only leading-capital names | `S`, `Ch`, `Capacity` |
| Private/protected data members | `m_` + `snake_case` | `m_level` |
| Public data members (struct fields) | `snake_case`, no prefix | `ratio{up, down}` |
| Public constants (namespace, class, static) | `snake_case`, no prefix (D30) | `rate_ratio`, `is_in_place` |
| Private or protected constants, and constants in `detail` | `k_` + `snake_case` (D30) | `k_block` |
| Enumerators | `snake_case` | `clocks::shared` |
| Macros | `ALL_CAPS`, prefixed `CATENAE_` | `CATENAE_VERSION` |

A leading capital letter means a template parameter and nothing else. Public constants read as
the standard library's traits do; `scripts/check_naming.py` checks the prefix by access, which
`clang-tidy` cannot see. A parameter never takes a member's or a field's name: the shadowing
warnings enforce it (PLAN.md section 7.3).

## 2. Layout

- **Indent:** 4 spaces, including inside namespaces.
- **Braces:** attached everywhere; only `else` breaks onto its own line. Every control-flow
  body is braced and expanded onto its own lines, guard clauses included.
- **Brace-init spacing:** `float x{0.0f}`, not `x {0.0f}`.
- **Column alignment:** consecutive declarations, assignments and trailing comments align.
- **Constructor initializers:** comma-first, one per line.
- **Pointers/references:** bound to the type: `const float* p`, `T& r`.
- **Member order:** `public`, `protected`, `private`; types, then constructors, then
  functions, then data members.
- **Column limit:** 120.
- **`const` placement:** west-const, by review.

## 3. Files

- **Extension:** `.h` for headers, `.cpp` for sources.
- **Include guard** (D40), not `#pragma once`: `CATENAE_<PATH>_<FILE>_H`, spelled from the
  header's path under `include/catenae/` (`catenae/detail/cx_math.h` is
  `CATENAE_DETAIL_CX_MATH_H`), or from the repository root for a header outside it
  (`tools/probe/stack_meter.h` is `CATENAE_TOOLS_PROBE_STACK_METER_H`). The `#endif` names it.
  `scripts/check_guards.py` checks both.
- **Banner** (D14, D33): the `@file` / `@brief` pair, then the copyright line. There is no
  license yet, so there is no SPDX line or any other license identifier.

  ```cpp
  /// @file version.h
  /// @brief The version a translation unit was compiled against, and the one it linked.
  // Copyright 2026 Timothy Place and the catenae contributors
  ```
- **Doc comments:** `///` with `@`-style commands (`@param`, `@return`, `@pre`, `@req`).
- **Include ordering:** the file's own header (in a `.cpp`), the C++ standard library,
  third-party headers, then catenae's, a blank line between groups.

## 4. Safety idioms

- **No exceptions, no RTTI, no heap** (D21, D22). What can fail returns a `status` or an
  `expected`, outside the audio path.
- **Views:** `catenae::span`, which has `std::span`'s interface (D2).
- **Checks:** the boundary and audit levels of D27, never `assert` and never a throw.
- **Conversions** between fixed-width types and `int`, `long` or `size_t` go through one named
  helper; float equality goes through one named helper; every shift of a signed value goes
  through one helper, and `scripts/check_shifts.py` rejects a raw one in the library.
- **Suppressions:** a diagnostic pragma or a `NOLINT` cites an entry in the deviation log,
  and `scripts/check_suppressions.py` counts them against it (D31).
