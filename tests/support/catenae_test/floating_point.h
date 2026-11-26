/// @file floating_point.h
/// @brief The battery as a host: it sets the thread's flush-to-zero bits, which catenae only
/// reads (AOU-008, PLAN.md 3.4).
// Copyright 2026 Timothy Place and the catenae contributors
//
// At start-up every test executable sets the bits to the build's statement
// (CATENAE_FLUSH_TO_ZERO), as a host does on its audio threads before they run, so the
// recursive stages' audit check passes on every leg. A test that pins what happens with the
// other setting sets it for its own scope:
//
//   const catenae_test::flush_to_zero_scope gradual(false);
//
// Where no bit governs a type (software float, Hexagon) the setting is the target's and these
// do nothing; catenae::detail::k_subnormal_control<S> says which.
#ifndef CATENAE_TESTS_SUPPORT_CATENAE_TEST_FLOATING_POINT_H
#define CATENAE_TESTS_SUPPORT_CATENAE_TEST_FLOATING_POINT_H

namespace catenae_test {

    /// Sets the calling thread's flush-to-zero bits (MXCSR's FTZ and DAZ, FPCR.FZ, FPSCR.FZ) on
    /// or off; nothing where the target has none. Returns whether they were on.
    bool set_flush_to_zero(bool on) noexcept;

    /// The setting for a scope, the previous one restored at its end.
    class flush_to_zero_scope {
      public:
        explicit flush_to_zero_scope(bool on) noexcept
            : m_was(set_flush_to_zero(on)) {}
        ~flush_to_zero_scope() { static_cast<void>(set_flush_to_zero(m_was)); }
        flush_to_zero_scope(const flush_to_zero_scope&)            = delete;
        flush_to_zero_scope& operator=(const flush_to_zero_scope&) = delete;

      private:
        bool m_was;
    };

} // namespace catenae_test

#endif // CATENAE_TESTS_SUPPORT_CATENAE_TEST_FLOATING_POINT_H
