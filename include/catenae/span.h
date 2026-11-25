/// @file span.h
/// @brief A view of contiguous elements with `std::span`'s interface, in C++17 (D2).
///
/// | Contract  |                                                                               |
/// |-----------|-------------------------------------------------------------------------------|
/// | Interface | `std::span`'s: extents, construction, element access, subviews, byte views  |
/// | Extent    | a compile-time size, or `dynamic_extent`                                      |
/// | Checks    | indices and subviews at the audit level (`check::index_out_of_range`)        |
/// | Differs   | no ranges constructor (C++20): a container with `data()` and `size()` instead |
/// | Rationale | D2, D22, D27                                                                  |
///
/// @req{CAT-SPAN-001} A span of static extent N has size N; one of dynamic extent has the size
/// it was made with; `size_bytes()` is the size times the element's size.
/// @req{CAT-SPAN-002} `first`, `last` and `subspan` view the elements `std::span`'s do, with a
/// static extent where their arguments are template arguments.
/// @req{CAT-SPAN-003} A span of `T` converts to a span of `const T`, and one of static extent to
/// one of dynamic extent, implicitly; a dynamic one converts to a static one only explicitly.
/// @req{CAT-SPAN-004} An index or a subview outside the span is an audit check.
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_SPAN_H
#define CATENAE_SPAN_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <type_traits>

#include "catenae/check.h"
#include "catenae/detail/abi.h"
#include "catenae/fwd.h"

namespace catenae {
    inline namespace CATENAE_ABI {

        namespace detail {

            template <typename T>
            struct is_span : std::false_type {};
            template <typename T, std::size_t E>
            struct is_span<span<T, E>> : std::true_type {};

            template <typename T>
            struct is_std_array : std::false_type {};
            template <typename T, std::size_t N>
            struct is_std_array<std::array<T, N>> : std::true_type {};

            /// A container a span of T may view: data() and size(), elements convertible as
            /// T*, and neither a span, a std::array nor a built-in array.
            template <typename C, typename T, typename = void>
            struct is_span_container : std::false_type {};
            template <typename C, typename T>
            struct is_span_container<
                C, T, std::void_t<decltype(std::declval<C&>().data()), decltype(std::declval<C&>().size())>>
                : std::bool_constant<
                      !is_span<std::remove_cv_t<C>>::value && !is_std_array<std::remove_cv_t<C>>::value
                      && !std::is_array<C>::value
                      && std::is_convertible<std::remove_pointer_t<decltype(std::declval<C&>().data())> (*)[],
                                             T (*)[]>::value> {};

            template <std::size_t Extent, std::size_t Offset, std::size_t Count>
            struct subspan_extent
                : std::integral_constant<
                      std::size_t,
                      Count != dynamic_extent ? Count : (Extent != dynamic_extent ? Extent - Offset : dynamic_extent)> {
            };

            /// Where a span's elements are, and how many: the count is stored only when the extent
            /// is dynamic, so a span of static extent is one pointer.
            template <typename T, std::size_t Extent>
            class span_storage {
              public:
                constexpr span_storage() noexcept = default;
                constexpr span_storage(T* from, std::size_t) noexcept
                    : m_data(from) {}
                constexpr T*                 data() const noexcept { return m_data; }
                static constexpr std::size_t size() noexcept { return Extent; }

              private:
                T* m_data = nullptr;
            };
            template <typename T>
            class span_storage<T, dynamic_extent> {
              public:
                constexpr span_storage() noexcept = default;
                constexpr span_storage(T* from, std::size_t count) noexcept
                    : m_data(from)
                    , m_count(count) {}
                constexpr T*          data() const noexcept { return m_data; }
                constexpr std::size_t size() const noexcept { return m_count; }

              private:
                T*          m_data  = nullptr;
                std::size_t m_count = 0;
            };

        } // namespace detail

        /// @brief A view of `Extent` (or a run-time number of) contiguous `T`.
        template <typename T, std::size_t Extent>
        class span {
          public:
            using element_type     = T;                               ///< an element, const if it is
            using value_type       = std::remove_cv_t<T>;             ///< an element's value
            using size_type        = std::size_t;                     ///< a count of elements
            using difference_type  = std::ptrdiff_t;                  ///< a distance between two
            using pointer          = T*;                              ///< to an element
            using const_pointer    = const T*;                        ///< to an element, read only
            using reference        = T&;                              ///< an element
            using const_reference  = const T&;                        ///< an element, read only
            using iterator         = T*;                              ///< a pointer: contiguous
            using reverse_iterator = std::reverse_iterator<iterator>; ///< from the back

            /// The span's compile-time extent.
            static constexpr std::size_t extent = Extent;

            /// An empty span; only of dynamic extent or extent 0.
            template <std::size_t E = Extent, std::enable_if_t<E == dynamic_extent || E == 0, int> = 0>
            constexpr span() noexcept {}

            /// The `count` elements from `from`.
            constexpr span(pointer from, size_type count) noexcept
                : m_storage(from, count) {
                detail::check_audit(Extent == dynamic_extent || count == Extent, check::index_out_of_range);
            }

            /// The elements from `from` up to `to`. A template, so that a count of 0 is a count
            /// and never a null pointer.
            template <typename P, std::enable_if_t<std::is_same<P, pointer>::value, int> = 0>
            constexpr span(pointer from, P to) noexcept
                : span(from, static_cast<size_type>(to - from)) {}

            /// A built-in array.
            template <std::size_t N, std::enable_if_t<Extent == dynamic_extent || N == Extent, int> = 0>
            constexpr span(element_type (&array)[N]) noexcept // implicit, as std::span's (DEV-012)
                : m_storage(array, N) {}

            /// A std::array.
            template <
                typename U, std::size_t N,
                std::enable_if_t<
                    (Extent == dynamic_extent || N == Extent) && std::is_convertible<U (*)[], T (*)[]>::value, int> = 0>
            constexpr span(std::array<U, N>& array) noexcept // implicit, as std::span's (DEV-012)
                : m_storage(array.data(), N) {}

            /// A const std::array.
            template <typename U, std::size_t N,
                      std::enable_if_t<(Extent == dynamic_extent || N == Extent)
                                           && std::is_convertible<const U (*)[], T (*)[]>::value,
                                       int> = 0>
            constexpr span(const std::array<U, N>& array) noexcept // implicit, as std::span's (DEV-012)
                : m_storage(array.data(), N) {}

            /// A container with data() and size(); explicit when the extent is static.
            template <typename C, std::size_t E = Extent,
                      std::enable_if_t<E == dynamic_extent && detail::is_span_container<C, T>::value, int> = 0>
            constexpr span(C& container) noexcept // implicit, as std::span's (DEV-012)
                : m_storage(container.data(), container.size()) {}
            /// A container with data() and size(), viewed at a static extent: explicit.
            template <typename C, std::size_t E = Extent,
                      std::enable_if_t<E != dynamic_extent && detail::is_span_container<C, T>::value, int> = 0>
            constexpr explicit span(C& container) noexcept
                : span(container.data(), container.size()) {}

            /// Another span, implicitly where nothing can go wrong (a wider const, a static extent
            /// to a dynamic one), explicitly from a dynamic extent to a static one.
            template <
                typename U, std::size_t N,
                std::enable_if_t<
                    (Extent == dynamic_extent || N == Extent) && std::is_convertible<U (*)[], T (*)[]>::value, int> = 0>
            constexpr span(const span<U, N>& other) noexcept // implicit, as std::span's (DEV-012)
                : m_storage(other.data(), other.size()) {}
            /// A span of dynamic extent viewed at a static one: explicit.
            template <typename U, std::size_t N,
                      std::enable_if_t<Extent != dynamic_extent && N == dynamic_extent
                                           && std::is_convertible<U (*)[], T (*)[]>::value,
                                       int> = 0>
            constexpr explicit span(const span<U, N>& other) noexcept
                : span(other.data(), other.size()) {}

            /// The same elements.
            constexpr span(const span&) noexcept = default;
            /// The same elements as `other`, which is unchanged.
            constexpr span& operator=(const span& other) noexcept = default;

            /// The first `Count` elements.
            template <std::size_t Count>
            constexpr span<T, Count> first() const noexcept {
                static_assert(Extent == dynamic_extent || Count <= Extent, "catenae: first<Count> beyond the extent");
                detail::check_audit(Count <= size(), check::index_out_of_range);
                return span<T, Count>(data(), Count);
            }
            /// The last `Count` elements.
            template <std::size_t Count>
            constexpr span<T, Count> last() const noexcept {
                static_assert(Extent == dynamic_extent || Count <= Extent, "catenae: last<Count> beyond the extent");
                detail::check_audit(Count <= size(), check::index_out_of_range);
                return span<T, Count>(data() + (size() - Count), Count);
            }
            /// `Count` elements from `Offset` (to the end when `Count` is `dynamic_extent`).
            template <std::size_t Offset, std::size_t Count = dynamic_extent>
            constexpr span<T, detail::subspan_extent<Extent, Offset, Count>::value> subspan() const noexcept {
                static_assert(Extent == dynamic_extent || Offset <= Extent,
                              "catenae: subspan<Offset> beyond the extent");
                static_assert(Extent == dynamic_extent || Count == dynamic_extent || Count <= Extent - Offset,
                              "catenae: subspan<Offset, Count> beyond the extent");
                detail::check_audit(Offset <= size() && (Count == dynamic_extent || Count <= size() - Offset),
                                    check::index_out_of_range);
                return span<T, detail::subspan_extent<Extent, Offset, Count>::value>(
                    data() + Offset, Count == dynamic_extent ? size() - Offset : Count);
            }
            /// The first `count` elements.
            constexpr span<T> first(size_type count) const noexcept {
                detail::check_audit(count <= size(), check::index_out_of_range);
                return span<T>(data(), count);
            }
            /// The last `count` elements.
            constexpr span<T> last(size_type count) const noexcept {
                detail::check_audit(count <= size(), check::index_out_of_range);
                return span<T>(data() + (size() - count), count);
            }
            /// `count` elements from `offset` (to the end when `count` is `dynamic_extent`).
            constexpr span<T> subspan(size_type offset, size_type count = dynamic_extent) const noexcept {
                detail::check_audit(offset <= size() && (count == dynamic_extent || count <= size() - offset),
                                    check::index_out_of_range);
                return span<T>(data() + offset, count == dynamic_extent ? size() - offset : count);
            }

            /// The number of elements.
            constexpr size_type size() const noexcept { return m_storage.size(); }
            /// The number of bytes the elements occupy.
            constexpr size_type size_bytes() const noexcept { return size() * sizeof(element_type); }
            /// Whether there are no elements.
            [[nodiscard]] constexpr bool empty() const noexcept { return size() == 0; }

            /// The element at `index`.
            constexpr reference operator[](size_type index) const noexcept {
                detail::check_audit(index < size(), check::index_out_of_range);
                return data()[index];
            }
            /// The first element.
            constexpr reference front() const noexcept {
                detail::check_audit(!empty(), check::index_out_of_range);
                return data()[0];
            }
            /// The last element.
            constexpr reference back() const noexcept {
                detail::check_audit(!empty(), check::index_out_of_range);
                return data()[size() - 1];
            }
            /// The first element's address.
            constexpr pointer data() const noexcept { return m_storage.data(); }

            /// The first element.
            constexpr iterator begin() const noexcept { return data(); }
            /// One past the last.
            constexpr iterator end() const noexcept { return data() + size(); }
            /// The last element, going back.
            constexpr reverse_iterator rbegin() const noexcept { return reverse_iterator(end()); }
            /// One before the first, going back.
            constexpr reverse_iterator rend() const noexcept { return reverse_iterator(begin()); }

          private:
            detail::span_storage<T, Extent> m_storage;
        };

        /// A built-in array deduces its extent.
        template <typename T, std::size_t N>
        span(T (&)[N]) -> span<T, N>;
        /// A std::array deduces its extent.
        template <typename T, std::size_t N>
        span(std::array<T, N>&) -> span<T, N>;
        /// A const std::array deduces its extent, read only.
        template <typename T, std::size_t N>
        span(const std::array<T, N>&) -> span<const T, N>;
        /// A container deduces a dynamic extent.
        template <typename C>
        span(C&) -> span<std::remove_pointer_t<decltype(std::declval<C&>().data())>>;

        /// The span's elements as bytes.
        template <typename T, std::size_t N>
        span<const std::byte, N == dynamic_extent ? dynamic_extent : N * sizeof(T)> as_bytes(span<T, N> s) noexcept {
            return span < const std::byte,
                   N == dynamic_extent ? dynamic_extent
                                       : N * sizeof(T) > (reinterpret_cast<const std::byte*>(s.data()), s.size_bytes());
        }
        /// The span's elements as bytes that can be written.
        template <typename T, std::size_t N, std::enable_if_t<!std::is_const<T>::value, int> = 0>
        span<std::byte, N == dynamic_extent ? dynamic_extent : N * sizeof(T)> as_writable_bytes(span<T, N> s) noexcept {
            return span < std::byte, N == dynamic_extent
                                         ? dynamic_extent
                                         : N * sizeof(T) > (reinterpret_cast<std::byte*>(s.data()), s.size_bytes());
        }

    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_SPAN_H
