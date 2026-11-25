/// @file catenae_c.cpp
/// @brief The fit milestone's C ABI (PLAN.md section 7.5): the definitions, expanded from
/// catenae_manifest.h.
// Copyright 2026 Timothy Place and the catenae contributors
#include "catenae_c.h"

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

#include "catenae/chain.h"
#include "catenae/delay.h"
#include "catenae/gain.h"
#include "catenae/map.h"
#include "strip.h"

/// The types the manifest's entries name: type-level choices only (PLAN.md 7.5).
namespace catenae_c_types {

    using strip = example::strip_t;

    inline constexpr auto strip_mono_configuration =
        catenae::gain<1>{1.0f, 96} | catenae::delay<1, 32>{} | catenae::map<1, example::halve>{example::halve{}};
    using strip_mono = std::remove_cv_t<decltype(strip_mono_configuration)>;

    /// Each entry's configuration as the library ships it, which configuration_copy constructs.
    template <typename T>
    struct shipped;
    template <>
    struct shipped<strip> {
        static constexpr const strip& value = example::strip;
    };
    template <>
    struct shipped<strip_mono> {
        static constexpr const strip_mono& value = strip_mono_configuration;
    };

} // namespace catenae_c_types

namespace {

    template <typename T>
    bool aligned(const void* p) noexcept {
        return reinterpret_cast<std::uintptr_t>(p) % alignof(T) == 0;
    }

    /// The configuration a caller holds: one configuration_copy constructed in its storage.
    template <typename T>
    catenae_c_status as_configuration(const void* p, std::size_t size, const T*& out) noexcept {
        if (p == nullptr) {
            return CATENAE_C_NULL;
        }
        if (size != sizeof(T)) {
            return CATENAE_C_WRONG_SIZE;
        }
        if (!aligned<T>(p)) {
            return CATENAE_C_MISALIGNED;
        }
        out = std::launder(static_cast<const T*>(p));
        return CATENAE_C_OK;
    }

    template <typename T>
    catenae_c_status as_storage(void* p, std::size_t size, T*& out) noexcept {
        if (p == nullptr) {
            return CATENAE_C_NULL;
        }
        if (size < sizeof(T)) {
            return CATENAE_C_WRONG_SIZE;
        }
        if (!aligned<T>(p)) {
            return CATENAE_C_MISALIGNED;
        }
        out = static_cast<T*>(p);
        return CATENAE_C_OK;
    }

    template <typename T>
    catenae_c_status copy_configuration(void* dst, std::size_t size) noexcept {
        if (dst == nullptr) {
            return CATENAE_C_NULL;
        }
        if (size < sizeof(T)) {
            return CATENAE_C_WRONG_SIZE;
        }
        if (!aligned<T>(dst)) {
            return CATENAE_C_MISALIGNED;
        }
        ::new (dst) T(catenae_c_types::shipped<T>::value);
        return CATENAE_C_OK;
    }

    template <typename T>
    catenae_c_status init(const void* configuration, std::size_t configuration_size, void* state,
                          std::size_t state_size) noexcept {
        const T*               c = nullptr;
        catenae::state_of<T>*  s = nullptr;
        const catenae_c_status a = as_configuration(configuration, configuration_size, c);
        if (a != CATENAE_C_OK) {
            return a;
        }
        const catenae_c_status b = as_storage(state, state_size, s);
        if (b != CATENAE_C_OK) {
            return b;
        }
        c->reset(*s);
        return CATENAE_C_OK;
    }

    template <typename T>
    catenae_c_status process(const void* configuration, std::size_t configuration_size, void* state,
                             std::size_t state_size, void* workspace, std::size_t workspace_size, const float* in,
                             std::size_t in_values, float* out, std::size_t out_values, std::size_t* written) noexcept {
        constexpr std::size_t     in_ch  = T::input_port::values_per_frame;
        constexpr std::size_t     out_ch = T::output_port::values_per_frame;
        const T*                  c      = nullptr;
        catenae::state_of<T>*     s      = nullptr;
        catenae::workspace_of<T>* w      = nullptr;
        catenae_c_status          r      = as_configuration(configuration, configuration_size, c);
        if (r == CATENAE_C_OK) {
            r = as_storage(state, state_size, s);
        }
        if (r == CATENAE_C_OK) {
            r = as_storage(workspace, workspace_size, w);
        }
        if (r != CATENAE_C_OK) {
            return r;
        }
        if ((in == nullptr && in_values != 0) || (out == nullptr && out_values != 0) || written == nullptr) {
            return CATENAE_C_NULL;
        }
        if (in_values % in_ch != 0) {
            return CATENAE_C_NOT_WHOLE_FRAMES;
        }
        if (out_values / out_ch < c->outputs_for(*s, in_values / in_ch)) {
            return CATENAE_C_OUTPUT_TOO_SMALL;
        }
        *written = c->process(*s, *w, catenae::span<const float>(in, in_values), catenae::span<float>(out, out_values))
                   * out_ch;
        return CATENAE_C_OK;
    }

    template <typename T>
    catenae_c_status set_level(void* configuration, std::size_t configuration_size, float value) noexcept {
        if (configuration == nullptr) {
            return CATENAE_C_NULL;
        }
        if (configuration_size != sizeof(T)) {
            return CATENAE_C_WRONG_SIZE;
        }
        if (!aligned<T>(configuration)) {
            return CATENAE_C_MISALIGNED;
        }
        std::launder(static_cast<T*>(configuration))->template stage<0>().set_level(value);
        return CATENAE_C_OK;
    }

} // namespace

// What a manifest's capacities promise, checked where the definitions are compiled.
#define CATENAE_C_CHECK(name, in_ch, out_ch, ccap, calign, scap, salign, wcap, walign, share)                          \
    static_assert(catenae_c_types::name::input_port::values_per_frame == (in_ch), "manifest: " #name " in_channels");  \
    static_assert(catenae_c_types::name::output_port::values_per_frame == (out_ch),                                    \
                  "manifest: " #name " out_channels");                                                                 \
    static_assert(sizeof(catenae_c_types::name) <= (ccap) && alignof(catenae_c_types::name) <= (calign),               \
                  "manifest: " #name "'s configuration exceeds its capacity on this target");                          \
    static_assert(sizeof(catenae::state_of<catenae_c_types::name>) <= (scap)                                           \
                      && alignof(catenae::state_of<catenae_c_types::name>) <= (salign),                                \
                  "manifest: " #name "'s state exceeds its capacity on this target");                                  \
    static_assert(sizeof(catenae::workspace_of<catenae_c_types::name>) <= (wcap)                                       \
                      && alignof(catenae::workspace_of<catenae_c_types::name>) <= (walign),                            \
                  "manifest: " #name "'s workspace exceeds its capacity on this target");                              \
    static_assert(std::is_same<catenae::workspace_of<catenae_c_types::name>,                                           \
                               catenae::workspace_of<catenae_c_types::share>>::value,                                  \
                  "manifest: " #name " shares a workspace with " #share ", whose type differs");
CATENAE_C_MANIFEST(CATENAE_C_CHECK)
#undef CATENAE_C_CHECK

extern "C" {

#define CATENAE_C_DEFINE(name, in_ch, out_ch, ccap, calign, scap, salign, wcap, walign, share)                         \
    size_t catenae_c_##name##_configuration_size(void) {                                                               \
        return sizeof(catenae_c_types::name);                                                                          \
    }                                                                                                                  \
    size_t catenae_c_##name##_configuration_align(void) {                                                              \
        return alignof(catenae_c_types::name);                                                                         \
    }                                                                                                                  \
    catenae_c_status catenae_c_##name##_configuration_copy(void* dst, size_t dst_size) {                               \
        return copy_configuration<catenae_c_types::name>(dst, dst_size);                                               \
    }                                                                                                                  \
    size_t catenae_c_##name##_state_size(void) {                                                                       \
        return sizeof(catenae::state_of<catenae_c_types::name>);                                                       \
    }                                                                                                                  \
    size_t catenae_c_##name##_state_align(void) {                                                                      \
        return alignof(catenae::state_of<catenae_c_types::name>);                                                      \
    }                                                                                                                  \
    size_t catenae_c_##name##_workspace_size(void) {                                                                   \
        return sizeof(catenae::workspace_of<catenae_c_types::name>);                                                   \
    }                                                                                                                  \
    size_t catenae_c_##name##_workspace_align(void) {                                                                  \
        return alignof(catenae::workspace_of<catenae_c_types::name>);                                                  \
    }                                                                                                                  \
    catenae_c_status catenae_c_##name##_init(const void* configuration, size_t configuration_size, void* state,        \
                                             size_t state_size) {                                                      \
        return init<catenae_c_types::name>(configuration, configuration_size, state, state_size);                      \
    }                                                                                                                  \
    catenae_c_status catenae_c_##name##_process(                                                                       \
        const void* configuration, size_t configuration_size, void* state, size_t state_size, void* workspace,         \
        size_t workspace_size, const float* in, size_t in_values, float* out, size_t out_values, size_t* written) {    \
        return process<catenae_c_types::name>(configuration, configuration_size, state, state_size, workspace,         \
                                              workspace_size, in, in_values, out, out_values, written);                \
    }
CATENAE_C_MANIFEST(CATENAE_C_DEFINE)
#undef CATENAE_C_DEFINE

#define CATENAE_C_DEFINE_SETTER(name, parameter, value_type)                                                           \
    catenae_c_status catenae_c_##name##_set_##parameter(void* configuration, size_t configuration_size,                \
                                                        value_type value) {                                            \
        return set_##parameter<catenae_c_types::name>(configuration, configuration_size, value);                       \
    }
CATENAE_C_PARAMETERS(CATENAE_C_DEFINE_SETTER)
#undef CATENAE_C_DEFINE_SETTER

#define CATENAE_C_COUNT_ENTRY(...) +1
#define CATENAE_C_COUNT_SETTER(...) +1
size_t catenae_c_entries(void) {
    return 0 CATENAE_C_MANIFEST(CATENAE_C_COUNT_ENTRY);
}
// Nine functions an entry, one a setter, and these two.
size_t catenae_c_functions(void) {
    return 9 * (0 CATENAE_C_MANIFEST(CATENAE_C_COUNT_ENTRY)) + (0 CATENAE_C_PARAMETERS(CATENAE_C_COUNT_SETTER)) + 2;
}
#undef CATENAE_C_COUNT_ENTRY
#undef CATENAE_C_COUNT_SETTER

} // extern "C"
