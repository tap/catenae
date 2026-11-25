# Copyright 2026 Timothy Place and the catenae contributors
#
# Cross-compilation toolchain for Qualcomm Hexagon: triple hexagon-unknown-linux-musl, built
# with the CodeLinaro "toolchain for hexagon" (clang + musl sysroot + LLVM runtimes), run under
# qemu-hexagon user-mode emulation. Carried from DspTap's cmake/hexagon-linux-musl.cmake
# (NOTICE.md), whose flags are MuTap's and SampleRateTap's.
#
# Point HEXAGON_TOOLCHAIN_ROOT (cache variable or environment) at the unpacked
# clang+llvm-*-cross-hexagon-unknown-linux-musl/x86_64-linux-gnu directory. A hosted Linux
# target, not bare metal: binaries print through stdio, exit normally and are linked
# statically (first-class with musl) so the emulator needs no sysroot path.
#
# Flags:
#   -mv68                   the newest revision the shipped sysroot libraries and
#                           qemu-hexagon agree on.
#   -mhvx -mhvx-length=128b the 128-byte HVX unit, for auto-vectorization.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR Hexagon)

if(NOT DEFINED HEXAGON_TOOLCHAIN_ROOT OR HEXAGON_TOOLCHAIN_ROOT STREQUAL "")
    set(HEXAGON_TOOLCHAIN_ROOT "$ENV{HEXAGON_TOOLCHAIN_ROOT}")
endif()
if(HEXAGON_TOOLCHAIN_ROOT STREQUAL "")
    message(FATAL_ERROR
        "Set HEXAGON_TOOLCHAIN_ROOT (cache variable or environment) to the unpacked CodeLinaro "
        "hexagon toolchain (clang+llvm-*-cross-hexagon-unknown-linux-musl/x86_64-linux-gnu)")
endif()
# try_compile projects re-read this file without the cache: pass the root on.
set(ENV{HEXAGON_TOOLCHAIN_ROOT} "${HEXAGON_TOOLCHAIN_ROOT}")

set(_catenae_hexagon_triple hexagon-unknown-linux-musl)
if(EXISTS "${HEXAGON_TOOLCHAIN_ROOT}/bin/${_catenae_hexagon_triple}-clang++")
    set(CMAKE_C_COMPILER "${HEXAGON_TOOLCHAIN_ROOT}/bin/${_catenae_hexagon_triple}-clang")
    set(CMAKE_CXX_COMPILER "${HEXAGON_TOOLCHAIN_ROOT}/bin/${_catenae_hexagon_triple}-clang++")
else()
    set(CMAKE_C_COMPILER "${HEXAGON_TOOLCHAIN_ROOT}/bin/clang")
    set(CMAKE_CXX_COMPILER "${HEXAGON_TOOLCHAIN_ROOT}/bin/clang++")
    set(CMAKE_C_COMPILER_TARGET ${_catenae_hexagon_triple})
    set(CMAKE_CXX_COMPILER_TARGET ${_catenae_hexagon_triple})
endif()

set(_catenae_hexagon_arch "-mv68 -mhvx -mhvx-length=128b")
set(CMAKE_C_FLAGS_INIT "${_catenae_hexagon_arch} -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_INIT "${_catenae_hexagon_arch} -ffunction-sections -fdata-sections")
# --eh-frame-hdr: not implied for -static links; kept from the Tap copy so a stack unwinder
# (a sanitizer, a debugger) finds the frame tables. catenae has no exceptions (D21).
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -Wl,--eh-frame-hdr -Wl,--gc-sections")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# The emulator CTest and gtest_discover_tests run each binary under: qemu-hexagon from the
# distribution's qemu-user (the test leg) or a plugin-enabled build first on PATH (the ratchet).
find_program(CATENAE_QEMU_HEXAGON NAMES qemu-hexagon-static qemu-hexagon)
if(CATENAE_QEMU_HEXAGON)
    set(CMAKE_CROSSCOMPILING_EMULATOR "${CATENAE_QEMU_HEXAGON}")
endif()
