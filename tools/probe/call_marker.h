/// @file call_marker.h
/// @brief The per-call markers the counting plugin brackets (bench/README.md).
// Copyright 2026 Timothy Place and the catenae contributors
//
// Carried from SampleRateTap's tools/probe/call_marker.h (NOTICE.md), names changed. The
// counting plugin (tools/qemu_insn_plugin/insn_count.c) finds these two functions by symbol
// and reports the most instructions executed between a call to catenae_call_begin() and the
// next catenae_call_end(): a callmax_* scenario's figure, the worst single call. Harness code,
// never shipped.
//
// They are extern "C" so the symbols are the plain names the plugin looks for, noinline so each
// is a real call the guest executes, and different (each stores its own value) so no
// identical-code folding can make them one address. Each is a store and a return, so a
// bracket's count includes the end marker's few instructions, the same in every call.
#ifndef CATENAE_TOOLS_PROBE_CALL_MARKER_H
#define CATENAE_TOOLS_PROBE_CALL_MARKER_H

#include <cstdint>

#if defined(_MSC_VER) && !defined(__clang__)
#define CATENAE_PROBE_NOINLINE __declspec(noinline)
#else
#define CATENAE_PROBE_NOINLINE __attribute__((noinline))
#endif

namespace catenae_probe {
    inline volatile std::uint32_t g_call_marker = 0;
} // namespace catenae_probe

extern "C" {
CATENAE_PROBE_NOINLINE inline void catenae_call_begin() noexcept {
    catenae_probe::g_call_marker = 1;
}
CATENAE_PROBE_NOINLINE inline void catenae_call_end() noexcept {
    catenae_probe::g_call_marker = 2;
}
}

#endif // CATENAE_TOOLS_PROBE_CALL_MARKER_H
