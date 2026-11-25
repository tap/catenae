/// @file strip_object_probe.cpp
/// @brief The strip as a desktop program would write it: one object holding its settings and
/// its memory, the delay line on the heap, built when the program starts (guide: Where memory
/// lives). Measured against strip_probe.cpp, never shipped.
// Copyright 2026 Timothy Place and the catenae contributors
//
// Built twice, as every probe set is (bench/CMakeLists.txt; scripts/ceilings.py):
//
//   catenae_probe_strip_object  the size probe: what the image holds, and the start-up function
//                               the object's constructor adds
//   catenae_stack_strip_object  the stack probe: process()'s high-water, and on newlib the bytes
//                               the heap holds for the object (CATENAE_HEAP), which no section of
//                               the image shows
//
// Its start-up has no static stack bound: operator new calls the new-handler through a pointer.
// scripts/ceilings.py says so for this probe set alone and records no start-up stack for it.
//
// The arithmetic is the strip's (a level that ramps over 96 frames, 32 frames of delay, a
// halving), written the way the guide's reader would write it first. It is not the chain's,
// bit for bit, and is not meant to be: what is measured is where its memory lives.
#include <array>
#include <cstddef>
#include <vector>

#include "call_marker.h"

#if CATENAE_PROBE_STACK
#include <cstdio>

#include "stack_meter.h"
#if defined(__NEWLIB__)
#include <malloc.h>
#endif
#endif

namespace catenae_probe {

    // ANCHOR: object
    /// The strip as one object: what it is set to and what it remembers, together.
    class strip_object {
      public:
        explicit strip_object(float level)
            : m_level(level)
            , m_target(level)
            , m_line(32 * 2, 0.0f) {}

        void set_level(float level) noexcept {
            m_target = level;
            m_step   = (m_target - m_level) / 96.0f;
            m_left   = 96;
        }

        void process(const float* in, float* out, std::size_t frames) noexcept {
            for (std::size_t f = 0; f < frames; ++f) {
                if (m_left > 0) {
                    --m_left;
                    m_level = m_left == 0 ? m_target : m_level + m_step;
                }
                for (std::size_t c = 0; c < 2; ++c) {
                    const float delayed     = m_line[2 * m_write + c];
                    m_line[2 * m_write + c] = in[2 * f + c] * m_level;
                    out[2 * f + c]          = delayed * 0.5f;
                }
                m_write = (m_write + 1) % 32;
            }
        }

      private:
        float              m_level;
        float              m_target;
        float              m_step = 0.0f;
        int                m_left = 0;
        std::vector<float> m_line; ///< 32 stereo frames, allocated by the constructor
        std::size_t        m_write = 0;
    };

    namespace {
        // One instance, a global: its constructor runs before main.
        strip_object s_strip{1.0f};
    } // namespace
    // ANCHOR_END: object

    /// A callback's block: 64 stereo frames.
    using block = std::array<float, 64 * 2>;

    CATENAE_PROBE_NOINLINE void probe_process(const block& in, block& out) noexcept;

    CATENAE_PROBE_NOINLINE void probe_process(const block& in, block& out) noexcept {
        s_strip.process(in.data(), out.data(), 64);
    }

} // namespace catenae_probe

int main() {
    catenae_probe::block in{};
    catenae_probe::block out{};
    in.fill(0.25f);
#if CATENAE_PROBE_STACK
#if defined(__NEWLIB__)
    // What the allocator holds after start-up, read before stdio allocates its own buffers:
    // the object's line and the allocator's bookkeeping for it.
    const std::size_t heap = mallinfo().uordblks;
#endif
    const std::size_t process = catenae_probe::high_water([&in, &out] { catenae_probe::probe_process(in, out); });
    std::printf("CATENAE_STACK process %lu\n", static_cast<unsigned long>(process));
#if defined(__NEWLIB__)
    std::printf("CATENAE_HEAP %lu\n", static_cast<unsigned long>(heap));
#endif
    std::printf("CATENAE_PROBE_DONE ok=%d\n", process < catenae_probe::paint_bytes ? 1 : 0);
#else
    catenae_probe::probe_process(in, out);
#endif
    return 0;
}
