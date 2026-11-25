# The warning sets (PLAN.md section 7.3, D31)

One home for each compiler's flags, read by both builds (`cmake/catenae_warnings.cmake` and
the `wscript`), so CMake and waf cannot drift apart. One flag per line; `#` starts a comment,
and an exclusion carries its reason on its own line. Every set is an error set (`-Werror`,
`/WX`) wherever catenae is the project being built, and a warning set where it is consumed.

| File | Applies to |
|---|---|
| `gcc.txt` | every catenae target built by GCC, hosted and `arm-none-eabi` |
| `gcc-library.txt` | GCC, the compiled core and the header-alone checks only (not tests or bench code) |
| `clang.txt` | every catenae target built by Clang or AppleClang, hosted and Hexagon: `-Weverything` less the written list |
| `clang-tests.txt` | Clang, test and bench code only: what a test framework needs off |
| `msvc.txt` | every catenae target built by MSVC |

`clang.txt` is the exclusion list catenae publishes: a consumer who builds catenae's headers
under `-Weverything` needs exactly these off.
