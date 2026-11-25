/// @file run_time_graph.cpp
/// @brief The run-time graph's nodes (run_time_graph.h): the strawman's definitions.
// Copyright 2026 Timothy Place and the catenae contributors
#include "run_time_graph.h"

#include <cstddef>

namespace example::run_time {

    const char* node::name() const noexcept {
        return "node";
    }

    bool graph::connect(node& next) noexcept {
        if (m_count == m_nodes.size()) {
            return false;
        }
        if (m_count > 0 && m_nodes[m_count - 1]->outputs() != next.inputs()) {
            return false; // a mismatch, found as the program runs
        }
        m_nodes[m_count] = &next;
        ++m_count;
        return true;
    }

    void graph::process(const float* in, float* out, std::size_t frames) noexcept {
        const float* from = in;
        for (std::size_t i = 0; i < m_count; ++i) {
            m_nodes[i]->process(from, out, frames); // an indirect call per node per block
            from = out;
        }
    }

    gain_node::gain_node(float level) noexcept
        : m_level(level)
        , m_current(level)
        , m_goal(level)
        , m_step(0.0f)
        , m_remaining(0) {}
    std::size_t gain_node::inputs() const noexcept {
        return 2;
    }
    std::size_t gain_node::outputs() const noexcept {
        return 2;
    }
    void gain_node::process(const float* in, float* out, std::size_t frames) noexcept {
        if (m_goal < m_level || m_goal > m_level) {
            m_goal      = m_level;
            m_step      = (m_level - m_current) / 96.0f;
            m_remaining = 96;
        }
        for (std::size_t f = 0; f < frames; ++f) {
            if (m_remaining > 0) {
                --m_remaining;
                m_current = m_goal - m_step * static_cast<float>(m_remaining);
            }
            out[2 * f]     = in[2 * f] * m_current;
            out[2 * f + 1] = in[2 * f + 1] * m_current;
        }
    }

    std::size_t delay_node::inputs() const noexcept {
        return 2;
    }
    std::size_t delay_node::outputs() const noexcept {
        return 2;
    }
    void delay_node::process(const float* in, float* out, std::size_t frames) noexcept {
        for (std::size_t f = 0; f < frames; ++f) {
            for (std::size_t c = 0; c < 2; ++c) {
                const float arriving    = in[2 * f + c];
                out[2 * f + c]          = m_line[2 * m_write + c];
                m_line[2 * m_write + c] = arriving;
            }
            m_write = m_write + 1 == 32 ? 0 : m_write + 1;
        }
    }

    std::size_t halve_node::inputs() const noexcept {
        return 2;
    }
    std::size_t halve_node::outputs() const noexcept {
        return 2;
    }
    void halve_node::process(const float* in, float* out, std::size_t frames) noexcept {
        for (std::size_t i = 0; i < 2 * frames; ++i) {
            out[i] = in[i] * 0.5f;
        }
    }

} // namespace example::run_time
