/// @file views.h
/// @brief How the chain reads and slices a port's views, one form per layout (PLAN.md section 4).
// Copyright 2026 Timothy Place and the catenae contributors
//
// An interleaved audio port, a spectrum and a wire port are viewed as one span of values, a
// frame `values_per_frame` of them; a planar audio port as `Ch` spans of one value a frame.
// The chain works on both through these: how many frames a view holds, whether an input is
// whole frames, a slice of frames, and a view of a buffer the workspace owns. A planar
// buffer of `cap` frames holds its channels end to end, channel c from value c x cap.
#ifndef CATENAE_DETAIL_VIEWS_H
#define CATENAE_DETAIL_VIEWS_H

#include <array>
#include <cstddef>
#include <utility>

#include "catenae/detail/abi.h"
#include "catenae/port.h"
#include "catenae/span.h"

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

            template <typename Port>
            inline constexpr bool k_is_planar =
                port_facts<Port>::k_kind == signal_kind::audio && port_facts<Port>::k_arrangement == layout::planar;

            /// A port viewed as one span of values.
            template <typename Port, bool = k_is_planar<Port>>
            struct views {
                using value_type                         = typename Port::value_type;
                using in_view                            = typename Port::input_view;
                using out_view                           = typename Port::output_view;
                static constexpr std::size_t k_per_frame = Port::values_per_frame;

                static constexpr std::size_t frames(in_view v) noexcept { return v.size() / k_per_frame; }
                static constexpr std::size_t frames(out_view v) noexcept { return v.size() / k_per_frame; }
                static constexpr bool        whole(in_view v) noexcept { return v.size() % k_per_frame == 0; }
                static constexpr in_view     slice(in_view v, std::size_t from, std::size_t n) noexcept {
                    return v.subspan(from * k_per_frame, n * k_per_frame);
                }
                static constexpr out_view slice(out_view v, std::size_t from, std::size_t n) noexcept {
                    return v.subspan(from * k_per_frame, n * k_per_frame);
                }
                static constexpr in_view in_of(const value_type* values, std::size_t, std::size_t n) noexcept {
                    return in_view(values, n * k_per_frame);
                }
                static constexpr out_view out_of(value_type* values, std::size_t, std::size_t n) noexcept {
                    return out_view(values, n * k_per_frame);
                }
                static constexpr in_view as_in(out_view v) noexcept { return in_view(v); }
            };

            /// A planar audio port: Ch spans, one per channel, of one value a frame.
            template <typename Port>
            struct views<Port, true> {
                using value_type                         = typename Port::value_type;
                using in_view                            = typename Port::input_view;
                using out_view                           = typename Port::output_view;
                static constexpr std::size_t k_channels  = Port::channels;
                static constexpr std::size_t k_per_frame = Port::values_per_frame;

                static constexpr std::size_t frames(in_view v) noexcept { return v[0].size(); }
                /// The frames every channel of an output has room for.
                static constexpr std::size_t frames(out_view v) noexcept {
                    std::size_t least = v[0].size();
                    for (const auto& channel : v) {
                        least = channel.size() < least ? channel.size() : least;
                    }
                    return least;
                }
                static constexpr bool whole(in_view v) noexcept {
                    for (const auto& channel : v) {
                        if (channel.size() != v[0].size()) {
                            return false;
                        }
                    }
                    return true;
                }
                static constexpr in_view slice(in_view v, std::size_t from, std::size_t n) noexcept {
                    in_view part{};
                    for (std::size_t c = 0; c < k_channels; ++c) {
                        part[c] = v[c].subspan(from, n);
                    }
                    return part;
                }
                static constexpr out_view slice(out_view v, std::size_t from, std::size_t n) noexcept {
                    out_view part{};
                    for (std::size_t c = 0; c < k_channels; ++c) {
                        part[c] = v[c].subspan(from, n);
                    }
                    return part;
                }
                static constexpr in_view in_of(const value_type* values, std::size_t cap, std::size_t n) noexcept {
                    in_view part{};
                    for (std::size_t c = 0; c < k_channels; ++c) {
                        part[c] = span<const value_type>(values + c * cap, n);
                    }
                    return part;
                }
                static constexpr out_view out_of(value_type* values, std::size_t cap, std::size_t n) noexcept {
                    out_view part{};
                    for (std::size_t c = 0; c < k_channels; ++c) {
                        part[c] = span<value_type>(values + c * cap, n);
                    }
                    return part;
                }
                static constexpr in_view as_in(out_view v) noexcept {
                    in_view part{};
                    for (std::size_t c = 0; c < k_channels; ++c) {
                        part[c] = v[c];
                    }
                    return part;
                }
            };

        } // namespace detail
    } // namespace CATENAE_ABI
} // namespace catenae

#endif // CATENAE_DETAIL_VIEWS_H
