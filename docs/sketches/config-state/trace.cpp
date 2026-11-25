// Copyright 2026 Timothy Place and the catenae contributors
// Prints, call by call, what a bursting region and its steady<> form do with 64-frame calls.
// The figure steady.svg is drawn from this output by figure.py. Throwaway.
#include <cstdio>
#include <cstdlib>

#include "sketch.h"

[[noreturn]] void catenae::check_failed(int /*code*/) noexcept {
    std::abort();
}

using namespace catenae;

struct halve {
    float operator()(float x) const noexcept { return 0.5f * x; }
};

int main() {
    using region_t = blockwise<128, halve>;
    constexpr auto bare   = chain(region_t{halve{}});
    constexpr auto smooth = chain(steady{region_t{halve{}}});
    static memory_of<decltype(bare)>   s_bare;
    static memory_of<decltype(smooth)> s_smooth;
    bare.reset(s_bare.state);
    smooth.reset(s_smooth.state);

    std::array<float, 64>  in{};
    std::array<float, 128> burst{};
    std::array<float, 64>  out{};
    std::printf("call,frames_in,bare_out,fifo_held,steady_out\n");
    for (int call = 1; call <= 8; ++call) {
        const std::size_t bare_out   = bare.process(s_bare, span<const float>(in), span<float>(burst));
        const std::size_t steady_out = smooth.process(s_smooth, in, out);
        const std::size_t held       = s_smooth.state.stages.first.held;
        std::printf("%d,%zu,%zu,%zu,%zu\n", call, in.size(), bare_out, held, steady_out);
    }
    return 0;
}
