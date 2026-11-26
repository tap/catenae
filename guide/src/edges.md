<!-- Copyright 2026 Timothy Place and the catenae contributors -->

# The edges

Audio does not reach a program as samples. It arrives as bytes in some agreed layout, and
leaves the same way. A chain's first and last stages are where that layout is dealt with,
so that nothing between them has to know it. This chapter takes the edge stages one family
at a time.

## Bytes: `unpack` and `pack`

A codec's DMA buffer, a file and a USB stream all hold samples in a *wire format*: so many
bits, in so many bytes, in one byte order. The constraint here is a buffer of bytes whose
layout the compiler knows nothing about.

The example reads 24-bit stereo that sits at the top of 32-bit little-endian words, halves
it in `float`, and writes 24-bit stereo packed in three bytes.

### What you would naturally write

You would cast the buffer to `const int32_t*` and loop. Each word is shifted down by 8 and
scaled to a `float`. On the way out, each value is scaled back and stored a byte at a time
with shifts and masks. It is a few lines, and it works on your desk.

### What it costs on the target

A loop like that is not slow, and this chapter claims no number for it. Its cost is that
every fact about the layout is in the arithmetic, where the compiler cannot see it.

- **The cast reads in the host's byte order.** The buffer's order belongs to the device
  that filled it. The two agree until the code moves to a host where they do not.
- **A wrong shift compiles.** Whether the 24 bits sit at the top of the word or the bottom
  is a constant in the loop. The wrong one is off by a factor of 256, and compiles.
- **Three-byte samples have no integer type.** The sign must be extended by hand.
- **Nothing ties the byte count to the frame count.** The loop trusts a length that
  someone computed elsewhere, from a sample size someone remembered.

Each of these is found by listening, on the device.

### What catenae does instead

The format is a type, and the two stages that take one are the chain's ends.

```cpp
{{#include ../../examples/edges.cpp:path}}
```

`unpack` reads bytes and writes the format's *native* samples. `pack` does the reverse.
Between them every sample is native: byte order and packing exist only at the two edges.
The stages in the middle are the ones from [Stages and chains](stages_and_chains.md).

The chain's outer ports are now *wire ports*, whose frames are counted in bytes. A stereo
frame of `s24le_in32_msb` is 8 bytes, and one of `s24le_packed` is 6. The callback's two
arrays are checked by the compiler against those ports, as the strip's `float` blocks
were.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/examples/catenae_example_edges
```

```text
in  frame 0: 5a 00 00 40 00 00 00 c0
out frame 0: 00 00 20 00 00 e0
CATENAE_EXAMPLE_DONE ok=1
```

The input frame is +0.5 on the left, `0x400000`, and -0.5 on the right, `0xc00000`. The
output is half of each, least significant byte first. The first input byte, `5a`, is the
byte of the word that holds no sample bits. `unpack` ignores it.

### Ten formats, two native ones

Namespace `wire` has ten formats: 16-bit, 24-bit packed in three bytes, 24 bits at the
least or the most significant end of a 32-bit word, and 32-bit, each little- or
big-endian. A 16-bit format's native format is `q15`. The others' is `q31`, with 24-bit
samples at its top, so one scale serves 24 and 32 bits alike. `convert` takes either
to `float`.

A sample is assembled from its bytes in the format's order, never read as a word. The
host's own byte order does not show.

### One rounding

`unpack` is exact for every format. `pack` is exact for 16 and 32 bits. A `q31` value
packed into 24 bits is narrowed once: rounded half up and saturated at 24 bits' limits.
That is the rule `convert` narrows by, so the chain has one rounding rule, not one per
edge. In a word wider than its sample, `pack` writes the unused byte as the sign's
extension when the sample is at the low end, and as zero when it is at the high end.

### A wire port inside a chain

Bytes join nothing. Put a stage that writes a wire port before one that reads a wire port,
and the joint fails with this fixed text:

```text
catenae: a wire port joins nothing inside a chain: only unpack reads one and pack writes one
```

The case `tests/compile_fail/joint_wire_inside.cpp` pins it. A stage in the middle of a
chain therefore never sees packed bytes, and never needs a version for each layout.

### From one wire to another

Changing a stream's byte order needs no stage of its own. A chain unpacks one format and
packs another: `unpack<wire::s24le_packed, 1>` into `pack<wire::s24be_packed, 1>` turns
the bytes `56 34 12` into `12 34 56`. The test `AChainEndsWithTheCallersBytes` in
`tests/test_pack.cpp` runs exactly that chain.

### What is linked

Each format has one compiled loop per direction, twenty in all, in `src/wire.cpp`. They
are compiled once, not once per chain. A linked image keeps only the loops it calls. The
probe image for these stages, `bench/probe/c2_probe.cpp`, reads and writes one format, and
calls two of the twenty.

The cards are on the pages of [`unpack`](api/unpack_8h.html) and
[`pack`](api/pack_8h.html), and the formats' requirements on
[`wire.h`'s](api/wire_8h.html).
