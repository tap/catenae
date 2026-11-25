# What a real `a78` CI key needs (not applied to any repository)

Measured locally on 2026-11-25. These files are a record; none is applied to DspTap or SampleRateTap.

1. `aarch64-cortex-a78-linux-gnu.cmake` as `cmake/aarch64-cortex-a78-linux-gnu.cmake` in each
   repository: aarch64-linux-gnu GCC, `-mcpu=cortex-a78`, static link.
2. The script patches: `sampleratetap-a78-scripts.patch` (icount.py's qemu_cmd and layout.py's
   target list), `dsptap-main-a78-icount.patch` / `dsptap-spike-a78-icount.patch` (MACHINES and
   qemu_cmd). `git apply` at each repository's root.
3. Workflow (bench.yml in DspTap, the ratchet job of ci.yml in SampleRateTap), modelled on the
   hexagon key:
   - apt: `g++-aarch64-linux-gnu libglib2.0-dev pkg-config ninja-build flex bison`
     (Ubuntu 24.04 gives aarch64-linux-gnu-g++ 13.3.0).
   - Ubuntu's packaged `qemu-aarch64` 8.2.2 has no plugin support (`qemu: unknown option
     'plugin'`), so build it as the hexagon job builds qemu-hexagon, from the same pinned
     source and digest (qemu-8.2.2.tar.xz, sha256 847346c1...bc4f3), cached on the digest:
     `./configure --target-list=aarch64-linux-user --enable-plugins --disable-docs
     --disable-tools --disable-system && ninja -C build qemu-aarch64`
     (about 6 minutes on 4 cores here). One configure with
     `--target-list=hexagon-linux-user,aarch64-linux-user` would serve both keys.
   - The plugin is the repository's own insn_count.c against the already pinned header
     (the header inside the 8.2.2 tarball has the pinned digest c53a2af1...606ff).
   - Configure: `-DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-cortex-a78-linux-gnu.cmake`
     and the same options as the other keys; then
     `icount.py --target a78 ... --prefix tap_dsp_layout_ --report-only` (DspTap) or
     `layout.py --target a78 ...` (SampleRateTap).
4. For a *gated* key (not only the informational layout step) two more things, untested here:
   DspTap's icount.py runs hexagon binaries from their build path with the caller's
   environment; a whole-run count under user-mode QEMU depends on argv[0] and the
   environment (SampleRateTap's script pins both), so a78 whole-run baselines need that
   pinning. Bracketed counts (the layout workloads) do not depend on it.
5. DspTap has no host target in icount.py; `dsptap_host_layout.py` is the callgrind
   command used for the x86-64 figures (`--toggle-collect='*layout_step*'`, as
   layout_main.cpp's comment names it), in SampleRateTap's pinned host environment.
6. The `-cpu` model is pinned to `cortex-a76` (QEMU 8.2.2 lists no cortex-a78). It matters:
   `neoverse-n1` gives identical bracketed counts, but `max` gives up to 1.4 % fewer
   (async 6 ch 68,106,174 -> 67,378,795; fir 6 ch 10,639,360 -> 10,485,760) because static
   glibc 2.39 dispatches memcpy/memset by CPU feature at start-up and `max` has SVE. A
   workload that calls no libc routine in its bracket (bw4) counts the same on all three.
