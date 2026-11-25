# Copyright 2026 Timothy Place and the catenae contributors
#
# Cross-compilation toolchain for Arm Cortex-M33 (bare metal, newlib + semihosting), executed
# on QEMU's MPS2+ AN505 board model: the Raspberry Pi Pico 2 (RP2350) class of core, a
# single-precision FPU, no FP64, no MVE. Carried from DspTap's cmake/arm-cortex-m33-mps2.cmake
# (NOTICE.md).
#
# Usage:
#   cmake -B build-m33 -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m33-mps2.cmake \
#         -DCMAKE_BUILD_TYPE=MinSizeRel
# with arm-none-eabi-g++ and qemu-system-arm on PATH.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-m33 -mthumb -mfloat-abi=hard -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")

get_filename_component(_catenae_platform "${CMAKE_CURRENT_LIST_DIR}/../platform" ABSOLUTE)
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "--specs=rdimon.specs -nostartfiles -Wl,--gc-sections -T${_catenae_platform}/mps2_an505.ld -x c ${_catenae_platform}/cortexm_startup.c -x none")

set(CMAKE_CROSSCOMPILING_EMULATOR "qemu-system-arm;-M;mps2-an505;-nographic;-semihosting;-kernel")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# One-shot CTest mode (no argv on bare metal; tests/CMakeLists.txt).
set(CATENAE_BARE_METAL ON)
