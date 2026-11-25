# Cross-compilation toolchain for a Cortex-A78 (AArch64 Linux, glibc), tests
# and workloads executed under qemu-aarch64 user-mode emulation.
#
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-cortex-a78-linux-gnu.cmake ...
# with aarch64-linux-gnu-g++ and a plugin-enabled qemu-aarch64 on PATH.
#
# QEMU 8.2 has no cortex-a78 model. The code generated for -mcpu=cortex-a78
# (Armv8.2-A with fp16, dotprod, rcpc, ssbs) runs on its cortex-a76 and
# neoverse-n1 models, which count alike. The model must still be pinned:
# static glibc picks memcpy and friends by CPU feature at start-up, so
# "-cpu max" (SVE) counts up to 1.4 % differently where a workload copies.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-a78")
set(CMAKE_CXX_FLAGS_INIT "-mcpu=cortex-a78")

# Static linking so the emulator needs no sysroot or loader configuration.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")

set(CMAKE_CROSSCOMPILING_EMULATOR "qemu-aarch64;-cpu;cortex-a76")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
