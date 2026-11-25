# Copyright 2026 Timothy Place and the catenae contributors
#
# Cross-compilation toolchain for Arm Cortex-M4 (bare metal, newlib + semihosting), executed
# on QEMU's MPS2 AN386 board model. Carried from DspTap's cmake/arm-cortex-m4-mps2.cmake
# (NOTICE.md). Two flavours, selected by the CATENAE_M4_FPU cache variable:
#   OFF (default)  -mfloat-abi=soft: no FPU. Every float and double operation is a library
#                  call; the fixed-point profiles' home, and the leg where a stray double
#                  shows up in the emulation budget.
#   ON             -mfpu=fpv4-sp-d16 -mfloat-abi=hard (M4F): a single-precision FPU, no FP64.
#
# Usage:
#   cmake -B build-m4 -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m4-mps2.cmake \
#         -DCMAKE_BUILD_TYPE=MinSizeRel [-DCATENAE_M4_FPU=ON]
# with arm-none-eabi-g++ and qemu-system-arm on PATH.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CATENAE_M4_FPU OFF CACHE BOOL "Cortex-M4F: fpv4-sp-d16 hard-float ABI (OFF = soft-float, no FPU)")
# Toolchain files are re-read inside try_compile projects, which do not see the parent cache;
# forward the switch so both reads pick the same flags.
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CATENAE_M4_FPU)

if(CATENAE_M4_FPU)
    set(_catenae_m4_float "-mfpu=fpv4-sp-d16 -mfloat-abi=hard")
else()
    set(_catenae_m4_float "-mfloat-abi=soft")
endif()
set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-m4 -mthumb ${_catenae_m4_float} -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")

get_filename_component(_catenae_platform "${CMAKE_CURRENT_LIST_DIR}/../platform" ABSOLUTE)
# `-x c` compiles the startup as C under the g++ driver: C guarantees the vector table's
# address-constant initializers are link-time constants, never dynamic initialization.
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "--specs=rdimon.specs -nostartfiles -Wl,--gc-sections -T${_catenae_platform}/mps2_an386.ld -x c ${_catenae_platform}/cortexm_startup.c -x none")

set(CMAKE_CROSSCOMPILING_EMULATOR "qemu-system-arm;-M;mps2-an386;-nographic;-semihosting;-kernel")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# One-shot CTest mode (no argv on bare metal; tests/CMakeLists.txt).
set(CATENAE_BARE_METAL ON)
