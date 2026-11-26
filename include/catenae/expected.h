/// @file expected.h
/// @brief `status`, and `expected<T, E>`: a value, or why there is none (D21, PLAN.md 3.3).
///
/// | Contract  |                                                                                    |
/// |-----------|------------------------------------------------------------------------------------|
/// | Interface | `std::expected`'s: `has_value()`, `operator bool`, `operator*`, `operator->`,     |
/// |           | `value()`, `error()`, `value_or()`, and `unexpected<E>`                           |
/// | Differs   | an empty access is a check in every build, never a throw (D21) or undefined       |
/// |           | an expected about to end gives a copy, not `T&&`, so nothing refers into it       |
/// | Types     | `T` and `E` trivially destructible (a design, a status), so it is `constexpr`     |
/// | Discard   | `expected` and `status` are `[[nodiscard]]`                                       |
/// | Rationale | D21, D37                                                                           |
///
/// @req{CAT-EXPECTED-001} An expected made from a value holds it: `has_value()` and `operator
/// bool` are true and `value()`, `operator*` and `operator->` reach it.
/// @req{CAT-EXPECTED-002} An expected made from `unexpected(e)` holds no value and its
/// `error()` is `e`; `value_or(v)` is `v`.
/// @req{CAT-EXPECTED-003} `value()`, `operator*` or `operator->` of an expected with no value,
/// and `error()` of one with a value, call `check_failed` in every build
/// (`check::expected_empty`, `check::expected_not_error`).
/// @req{CAT-EXPECTED-004} An expected is usable in a constant expression, and ignoring an
/// expected or a status that a call returns is a warning, so an error under catenae's sets.
/// @req{CAT-EXPECTED-005} A status is one byte, its codes the numbers listed here from 1: a
/// code is added with a new number, never by renumbering one.
/// @req{CAT-EXPECTED-006} `value()`, `operator*` and `error()` of an expected about to end (an
/// rvalue) return a copy, so `const auto& d = *make(...)` outlives the expected it came from;
/// of one that lives on (an lvalue), a reference.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_EXPECTED_H
#define CATENAE_EXPECTED_H

#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

#include "catenae/check.h"
#include "catenae/detail/abi.h"
#include "catenae/fwd.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// Why a design could not be made, or a call outside the audio path failed. A design's
        /// card names the codes its make() returns; a new code is added by documenting it there.
        enum class status : std::uint8_t { // [[nodiscard]], on its first declaration (catenae/fwd.h)
            invalid_argument = 1,          ///< an argument no design can be made from
            out_of_range     = 2,          ///< an argument outside the range the design supports
            unsupported      = 3,          ///< a combination the build or the format does not provide
            size_mismatch    = 4,          ///< storage of the wrong size or alignment (the C ABI, section 7.5)
        };

        /// The error an expected is made from.
        template <typename E>
        class unexpected {
          public:
            /// The error `error_value`.
            constexpr explicit unexpected(const E& error_value) noexcept
                : m_error(error_value) {}
            /// The error.
            constexpr const E& error() const noexcept { return m_error; }

          private:
            E m_error;
        };

        /// `unexpected(status::out_of_range)` is an `unexpected<status>`.
        template <typename E>
        unexpected(E) -> unexpected<E>;

        /// @brief A `T`, or the `E` that says why there is none.
        template <typename T, typename E>
        class [[nodiscard]] expected {
            static_assert(std::is_trivially_destructible<T>::value && std::is_trivially_destructible<E>::value,
                          "catenae: an expected holds trivially destructible types (a design, a status)");
            static_assert(!std::is_reference<T>::value && !std::is_same<std::remove_cv_t<T>, void>::value,
                          "catenae: an expected holds an object");

          public:
            using value_type = T; ///< what it holds when it has a value
            using error_type = E; ///< what it holds when it has none

            /// A value.
            constexpr expected(const T& v) noexcept // implicit, as std::expected's (DEV-012)
                : m_value(v)
                , m_has_value(true) {}
            /// An error.
            template <typename G, std::enable_if_t<std::is_convertible<const G&, E>::value, int> = 0>
            constexpr expected(const unexpected<G>& u) noexcept // implicit, as std::expected's (DEV-012)
                : m_error(u.error())
                , m_has_value(false) {}

            /// Whether it holds a value.
            constexpr bool has_value() const noexcept { return m_has_value; }
            /// Whether it holds a value.
            constexpr explicit operator bool() const noexcept { return m_has_value; }

            /// The value; a check in every build when there is none.
            constexpr const T& value() const& noexcept {
                detail::check_always(m_has_value, check::expected_empty);
                return m_value;
            }
            /// The value; a check in every build when there is none.
            constexpr T& value() & noexcept {
                detail::check_always(m_has_value, check::expected_empty);
                return m_value;
            }
            /// A copy of the value, from an expected about to end, so nothing refers into it.
            constexpr T value() && noexcept {
                detail::check_always(m_has_value, check::expected_empty);
                return m_value;
            }
            /// The value, as value().
            constexpr const T& operator*() const& noexcept { return value(); }
            /// The value, as value().
            constexpr T& operator*() & noexcept { return value(); }
            /// A copy of the value, as value() of an expected about to end.
            constexpr T operator*() && noexcept {
                detail::check_always(m_has_value, check::expected_empty);
                return m_value;
            }
            /// The value's members, as value().
            constexpr const T* operator->() const noexcept { return std::addressof(value()); }
            /// The value's members, as value().
            constexpr T* operator->() noexcept { return std::addressof(value()); }

            /// Why there is no value; a check in every build when there is one.
            constexpr const E& error() const& noexcept {
                detail::check_always(!m_has_value, check::expected_not_error);
                return m_error;
            }
            /// A copy of why there is no value, from an expected about to end.
            constexpr E error() && noexcept {
                detail::check_always(!m_has_value, check::expected_not_error);
                return m_error;
            }

            /// The value, or `fallback` when there is none.
            template <typename U>
            constexpr T value_or(U&& fallback) const noexcept {
                if (m_has_value) {
                    return m_value;
                }
                const T alternative(std::forward<U>(fallback));
                return alternative;
            }

          private:
            union {
                T m_value;
                E m_error;
            };
            bool m_has_value;
        };

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_EXPECTED_H
