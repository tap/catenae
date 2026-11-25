/// @file run_time_graph.h
/// @brief What one would naturally write: a graph of nodes joined at run time (guide: Stages
/// and chains). A strawman: the same arithmetic as strip.h, checked and dispatched as it runs.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_EXAMPLES_RUN_TIME_GRAPH_H
#define CATENAE_EXAMPLES_RUN_TIME_GRAPH_H

#include <array>
#include <cstddef>
#include <cstdint>

namespace example::run_time {

    // ANCHOR: node
    /// A node of a graph: its channel counts are numbers it reports when asked.
    class node {
      public:
        virtual const char* name() const noexcept; ///< what the graph reports a mismatch with
        virtual std::size_t inputs() const noexcept                                           = 0;
        virtual std::size_t outputs() const noexcept                                          = 0;
        virtual void        process(const float* in, float* out, std::size_t frames) noexcept = 0;

      protected:
        node()                       = default;
        ~node()                      = default;
        node(const node&)            = default;
        node& operator=(const node&) = default;
    };

    /// A graph: nodes joined at run time, each joint checked as it is made.
    class graph {
      public:
        /// Appends a node; false when its inputs do not match what the graph writes so far.
        bool connect(node& next) noexcept;
        /// Runs every node over the block in turn, in place in `out`.
        void process(const float* in, float* out, std::size_t frames) noexcept;

      private:
        std::array<node*, 8> m_nodes{};
        std::size_t          m_count = 0;
    };
    // ANCHOR_END: node

    /// A level, ramped over 96 frames when it changes.
    class gain_node final : public node {
      public:
        explicit gain_node(float level) noexcept;
        std::size_t inputs() const noexcept override;
        std::size_t outputs() const noexcept override;
        void        process(const float* in, float* out, std::size_t frames) noexcept override;

      private:
        float         m_level;
        float         m_current;
        float         m_goal;
        float         m_step;
        std::uint32_t m_remaining;
    };

    /// A fixed delay of 32 frames.
    class delay_node final : public node {
      public:
        std::size_t inputs() const noexcept override;
        std::size_t outputs() const noexcept override;
        void        process(const float* in, float* out, std::size_t frames) noexcept override;

      private:
        std::array<float, 32 * 2> m_line{};
        std::size_t               m_write = 0;
    };

    /// Half of what comes in.
    class halve_node final : public node {
      public:
        std::size_t inputs() const noexcept override;
        std::size_t outputs() const noexcept override;
        void        process(const float* in, float* out, std::size_t frames) noexcept override;
    };

} // namespace example::run_time

#endif // CATENAE_EXAMPLES_RUN_TIME_GRAPH_H
