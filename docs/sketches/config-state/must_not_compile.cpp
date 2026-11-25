// Copyright 2026 Timothy Place and the catenae contributors
// What the sketch refuses at compile time. Each CASE must fail with the message verify.sh
// expects; CASE 0 is the control and must compile. Throwaway.
#include "sketch.h"

[[noreturn]] void catenae::check_failed(int /*code*/) noexcept {
    for (;;) {
    }
}

using namespace catenae;

struct same {
    float operator()(float x) const noexcept { return x; }
};

#if CASE == 0
// The control: everything the other cases get wrong, done right.
constexpr auto ok = map<2, same>{same{}} | map<2, same>{same{}};
static memory_of<decltype(ok)> s_ok;
std::size_t                    use(const std::array<float, 8>& in, std::array<float, 8>& out) noexcept {
    return ok.process(s_ok, in, out);
}

#elif CASE == 1
// A joint: two channels cannot feed one. Forming the chain's type is enough; nothing is called.
constexpr auto bad = map<2, same>{same{}} | map<1, same>{same{}};

#elif CASE == 2
// Down then up is 1:1 but works in pairs: three frames are not a whole number of them.
constexpr auto down_up = down2<1>{} | up2<1>{};
static memory_of<decltype(down_up)> s_memory;
std::size_t                         use(const std::array<float, 3>& in, std::array<float, 4>& out) noexcept {
    return down_up.process(s_memory, in, out);
}

#elif CASE == 3
// A block stage of 128 called with 64-frame fixed blocks.
constexpr auto region = chain(blockwise<128, same>{same{}});
static memory_of<decltype(region)> s_memory;
std::size_t                        use(const std::array<float, 64>& in, std::array<float, 64>& out) noexcept {
    return region.process(s_memory, in, out);
}

#elif CASE == 4
// A table whose poles are outside the unit circle cannot initialise a constant design.
constexpr auto k_unstable = lowpass_design<float>::from_table({0.1f, 0.2f, 0.1f, -2.5f, 1.7f}, 0.0f);

#elif CASE == 5
// One side of | is not a stage.
constexpr auto bad = map<2, same>{same{}} | 3;

#elif CASE == 6
// The output block is too small for the input block.
constexpr auto up = chain(up2<2>{});
static memory_of<decltype(up)> s_memory;
std::size_t                    use(const std::array<float, 8>& in, std::array<float, 8>& out) noexcept {
    return up.process(s_memory, in, out);
}

#elif CASE == 7
// A state that is not plain storage (here: it has a constructor of its own).
struct fancy {
    using input_port  = port<float, 1>;
    using output_port = port<float, 1>;
    static constexpr ratio       rate_ratio{1, 1};
    static constexpr std::size_t input_group = 1;
    static constexpr ratio       max_shortfall{0, 1};
    struct state {
        state() noexcept {}
        state(const state& /*unused*/) noexcept {}
        float kept = 1.0f;
    };
    using workspace = no_workspace;
    std::size_t run(state& /*unused*/, workspace& /*unused*/, span<const float> in, span<float> /*unused*/) const noexcept {
        return in.size();
    }
    static constexpr std::size_t max_outputs_for(std::size_t n) noexcept { return n; }
    static constexpr std::size_t outputs_for(const state& /*unused*/, std::size_t n) noexcept { return n; }
    static constexpr std::size_t frames_needed(const state& /*unused*/, std::size_t k) noexcept { return k; }
};
template <>
struct catenae::is_stage<fancy> : std::true_type {};
constexpr auto                    wrapped = chain(fancy{});
static state_of<decltype(wrapped)> s_state;
void                               use() noexcept {
    wrapped.reset(s_state);
}

#elif CASE == 8
// A mailbox value must be trivially copyable.
struct owner {
    owner(const owner& /*unused*/) noexcept {}
    int value;
};
static mailbox<owner> s_box;
void                  use(const owner& o) noexcept {
    s_box.post(o);
}
#endif
