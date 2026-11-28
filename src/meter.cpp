/// @file meter.cpp
/// @brief The meters' conversions to decibels, off the audio path (include/catenae/detail/meter.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include "catenae/detail/meter.h"

#include <cmath>

namespace catenae {
    inline namespace CATENAE_ABI {
        namespace detail {

            float amplitude_db(float ratio) noexcept {
                return 20.0f * std::log10(ratio);
            }
            double amplitude_db(double ratio) noexcept {
                return 20.0 * std::log10(ratio);
            }
            float power_db(float ratio) noexcept {
                return 10.0f * std::log10(ratio);
            }
            double power_db(double ratio) noexcept {
                return 10.0 * std::log10(ratio);
            }

        } // namespace detail
    } // namespace CATENAE_ABI
} // namespace catenae
