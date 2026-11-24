#!/usr/bin/env bash
# Builds the sketch for a Cortex-M33 and prints where the memory landed and what each file needs.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out="${1:-/tmp/catenae-sketch}"
mkdir -p "$out"
for unit in embedded embedded_design; do
    arm-none-eabi-g++ -std=c++17 -mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16 -Os \
        -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
        -Wall -Wextra -Wshadow -Wconversion -Wdouble-promotion -Werror -fstack-usage \
        -c "$unit.cpp" -o "$out/$unit.o"
done
echo "--- data of the audio path (embedded.o)"
arm-none-eabi-nm -S --size-sort -C "$out/embedded.o" | while read -r _ size kind name; do
    case "$kind" in
        b | B) section=".bss   " ;;
        d | D) section=".data  " ;;
        r | R) section=".rodata" ;;
        *) continue ;;
    esac
    printf "%6d bytes  %s  %s\n" "$((16#$size))" "$section" "$name"
done
echo "--- stack frames"
cat "$out"/*.su | awk -F'\t' '$1 ~ /callback|set_/ {sub(/^[^:]*:[0-9]*:[0-9]*:/, "", $1); printf "%4d bytes  %s\n", $2, $1}'
echo "--- software floating point and libm that each file calls"
for unit in embedded embedded_design; do
    printf "%s.o: " "$unit"
    arm-none-eabi-nm -u "$out/$unit.o" | awk '{print $2}' | grep -E "^__aeabi_[df]|^tan|^tanh" | tr '\n' ' ' || true
    echo
done
