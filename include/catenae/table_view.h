/// @file table_view.h
/// @brief `table_view<T, N>`: N values a configuration reads and does not own, made only from
/// storage that outlives it; and `holds_view<T>`, which says a type holds one (D54, D44).
///
/// | Contract  |                                                                                |
/// |-----------|--------------------------------------------------------------------------------|
/// | View      | a pointer to `N` values of `const T`: trivially copyable, `constexpr`         |
/// | Made from | an lvalue `std::array<T, N>` or `T[N]` (implicit), or storage the caller wrote, |
/// |           | `span<T, N>` (explicit); never a temporary, whose overloads are deleted        |
/// | Read      | `operator[]`, checked at audit; `data()`, `size()`, `values()` as a span       |
/// | Opt-in    | `holds_view<T>`, specialised by the author of a type whose copy refers to      |
/// |           | storage it does not own; chains compose it and a mailbox refuses it            |
/// | Rationale | D44, D54, PLAN.md 3.3 and 6.3                                                  |
///
/// Why not a `span<const T>`: a span of const values binds a temporary array, as `std::span`
/// does, so a constructor that took one could not refuse a table that ends before it does.
///
/// @req{CAT-TABLE-VIEW-001} A `table_view<T, N>` is made from an lvalue `std::array<T, N>` or
/// `T[N]`, or explicitly from writable storage (`span<T, N>`), and never from a temporary, so a
/// constructor that takes one refuses a table that would end before the view does.
/// @req{CAT-TABLE-VIEW-002} It is a pointer: trivially copyable, usable in a constant
/// expression, its `N` values read in place; an index at or past `N` is an audit check
/// (`check::index_out_of_range`).
/// @req{CAT-TABLE-VIEW-003} `holds_view<T>` is false unless specialised; it is true for a
/// `table_view`, and a chain, `steady<>` or `parallel<>` holds a view when any of its stages
/// does.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_TABLE_VIEW_H
#define CATENAE_TABLE_VIEW_H

#include <array>
#include <cstddef>
#include <type_traits>

#include "catenae/check.h"
#include "catenae/detail/abi.h"
#include "catenae/fwd.h"
#include "catenae/span.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        /// Whether a copy of a T refers to storage it does not own: false unless its author
        /// specialises it. A mailbox refuses such a value (D44); a table design is replaced
        /// through the acknowledgement of PLAN.md 6.3 instead.
        template <typename T>
        struct holds_view : std::false_type {};

        /// @brief N values of `const T` that a configuration reads and does not own.
        template <typename T, std::size_t N>
        class table_view {
            static_assert(!std::is_const<T>::value && !std::is_volatile<T>::value,
                          "catenae: a table_view is of the values' own type; it reads them as const");
            static_assert(N >= 1, "catenae: a table_view views at least one value");

          public:
            using value_type                    = T; ///< what the table holds
            static constexpr std::size_t extent = N; ///< how many

            /// A table in an array that outlives the view: a constant, a static, ROM.
            constexpr table_view(const std::array<T, N>& table) noexcept // implicit, as span's (DEV-012)
                : m_values(table.data()) {}
            /// A temporary table: refused, the view would outlive it.
            table_view(const std::array<T, N>&&) = delete;
            /// A table in a built-in array that outlives the view.
            constexpr table_view(const T (&table)[N]) noexcept // implicit, as span's (DEV-012)
                : m_values(table) {}
            /// A temporary built-in array: refused.
            table_view(const T (&&)[N]) = delete;
            /// Storage the caller wrote, a design's make() among them: writable, so not a temporary.
            explicit constexpr table_view(span<T, N> written) noexcept
                : m_values(written.data()) {}

            /// The value at `i`; past the table is an audit check.
            constexpr const T& operator[](std::size_t i) const noexcept {
                detail::check_audit(i < N, check::index_out_of_range);
                return m_values[i];
            }
            /// The first value.
            constexpr const T* data() const noexcept { return m_values; }
            /// N.
            static constexpr std::size_t size() noexcept { return N; }
            /// The table as a span, for a kernel that reads it.
            constexpr span<const T, N> values() const noexcept { return span<const T, N>(m_values, N); }

          private:
            const T* m_values; // the caller's table
        };

        template <typename T, std::size_t N>
        struct holds_view<table_view<T, N>> : std::true_type {};

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_TABLE_VIEW_H
