#!/usr/bin/env bash
# Builds the sketch for a Cortex-M33 and prints where the memory landed, what each call costs
# in code and stack, and what the float image links with and without a designer.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out="${1:-/tmp/catenae-sketch}"
mkdir -p "$out"
target="-mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16 -Os"
for unit in embedded embedded_design embedded_main; do
    arm-none-eabi-g++ -std=c++17 $target -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
        -Wall -Wextra -Wshadow -Wconversion -Wdouble-promotion -Werror -fstack-usage -fcallgraph-info=su \
        -c "$unit.cpp" -o "$out/$unit.o"
done
arm-none-eabi-g++ -std=c++17 $target -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -DWITH_DESIGNER \
    -c embedded_main.cpp -o "$out/embedded_main_designer.o"

echo "--- start-up code: static constructors in embedded.o at each standard (every static is CATENAE_SKETCH_CONSTINIT)"
for standard in 17 20 23; do
    arm-none-eabi-g++ -std=c++$standard $target -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
        -c embedded.cpp -o "$out/embedded_$standard.o"
    count=$(arm-none-eabi-size -A "$out/embedded_$standard.o" | grep -c -E "init_array|text\.startup" || true)
    printf "C++%s: %d start-up sections\n" "$standard" "$count"
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

echo "--- each call: code (its own function) and stack frame (its own frame, not the call path's)"
arm-none-eabi-nm -S -C "$out/embedded.o" "$out/embedded_design.o" | awk '$3 ~ /^[tT]$/ {print $2, $4}' > "$out/code.txt"
cat "$out/embedded.su" "$out/embedded_design.su" | awk -F'\t' '{sub(/^[^:]*:[0-9]*:[0-9]*:/, "", $1); print $2 "\t" $1}' |
    while IFS=$'\t' read -r frame signature; do
        name="${signature#* }"
        name="${name%%(*}"
        case "$name" in
            *callback* | reset_* | set_* | post_* | relay_*) ;;
            *) continue ;;
        esac
        size=$(awk -v n="$name" '{split($2, a, "("); if (a[1] == n) print $1}' "$out/code.txt" | head -1)
        printf "%5d bytes of code  %4d bytes of stack  %s\n" "$((16#${size:-0}))" "$frame" "$name"
    done

echo "--- each call: static worst case down its call path (stack_path.py)"
python3 stack_path.py "$out/embedded.ci" "$out/embedded_design.ci"

echo "--- software floating point and libm that each file calls"
for unit in embedded embedded_design; do
    printf "%s.o: " "$unit"
    arm-none-eabi-nm -u "$out/$unit.o" | awk '{print $2}' | grep -E "^__aeabi_[df]|^tan|^tanh" | tr '\n' ' ' || true
    echo
done

echo "--- sizes on this target"
arm-none-eabi-g++ -std=c++17 $target -fno-exceptions -fno-rtti -DSIZES -c embedded.cpp -o "$out/sizes.o"
arm-none-eabi-nm -S -C "$out/sizes.o" | while read -r _ size _ name; do
    case "$name" in size_of_*) printf "%6d bytes  %s\n" "$((16#$size))" "${name#size_of_}" ;; esac
done

echo "--- the linked image (--gc-sections), with table designs only and with the run-time designer"
doubles='^(__aeabi_d|__aeabi_f2d|__aeabi_d2f|__aeabi_ui2d|__aeabi_i2d|tan$|__kernel_tan|__ieee754_rem_pio2|__kernel_rem_pio2|floor$|scalbn$)'
for image in tables designer; do
    main="$out/embedded_main.o"
    [ "$image" = designer ] && main="$out/embedded_main_designer.o"
    arm-none-eabi-g++ $target --specs=nosys.specs --specs=nano.specs -Wl,--gc-sections \
        "$out/embedded.o" "$out/embedded_design.o" "$main" -o "$out/$image.elf" > /dev/null 2>&1
    text=$(arm-none-eabi-size "$out/$image.elf" | awk 'NR == 2 {print $1}')
    count=$(arm-none-eabi-nm "$out/$image.elf" | awk '{print $3}' | grep -c -E "$doubles" || true)
    printf "%-8s %6d bytes of text, %2d software-double or double-libm symbols\n" "$image" "$text" "$count"
done
