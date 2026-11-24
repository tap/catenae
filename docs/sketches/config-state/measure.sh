#!/usr/bin/env bash
# Builds embedded.cpp for a Cortex-M33 and prints where the chain's memory landed.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out="${1:-/tmp/catenae-sketch}"
mkdir -p "$out"
arm-none-eabi-g++ -std=c++17 -mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16 -Os \
    -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
    -Wall -Wextra -Wshadow -Wconversion -Wdouble-promotion -Werror -fstack-usage \
    -c embedded.cpp -o "$out/embedded.o"
arm-none-eabi-size -A "$out/embedded.o" | grep -E '^\.(text|rodata|data|bss)' | awk '{printf "%-40s %6d\n", $1, $2}'
echo "--- symbols"
arm-none-eabi-nm -S --size-sort -C "$out/embedded.o" | tail -4
echo "--- stack of the callback"
grep audio_callback "$out/embedded.su" | awk '{print $(NF-1), "bytes,", $NF}'
