// Copyright 2026 Timothy Place and the catenae contributors
// Two real threads through a mailbox. Every value taken must be whole (never half of one post
// and half of another) and never older than the one before. Throwaway.
//   g++ -std=c++17 -O2 -pthread -fsanitize=thread mailbox_stress.cpp -o stress && ./stress
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "sketch.h"

[[noreturn]] void catenae::check_failed(int /*code*/) noexcept {
    std::abort();
}

struct reading {
    std::uint32_t number;
    std::uint32_t inverse; // always ~number: a torn value would break this
    std::uint32_t padding[14];
};

int main() {
    static catenae::mailbox<reading> s_box;
    constexpr std::uint32_t          k_posts = 2000000;

    std::thread poster([] {
        for (std::uint32_t i = 1; i <= k_posts; ++i) {
            reading r{};
            r.number  = i;
            r.inverse = ~i;
            s_box.post(r);
        }
    });

    std::uint32_t last = 0, taken = 0, torn = 0, backwards = 0;
    while (last < k_posts) {
        s_box.take([&](const reading& r) noexcept {
            torn += (r.inverse != ~r.number) ? 1U : 0U;
            backwards += (r.number < last) ? 1U : 0U;
            last = r.number;
            ++taken;
        });
    }
    poster.join();
    std::printf("posted %u, took %u (the rest were overtaken by newer ones); torn %u, out of order %u\n", k_posts, taken, torn,
                backwards);
    return (torn == 0 && backwards == 0) ? 0 : 1;
}
