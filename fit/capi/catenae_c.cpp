/// @file catenae_c.cpp
/// @brief The fit milestone's C ABI (PLAN.md section 7.5): the definitions, expanded from
/// catenae_manifest.h.
// Copyright 2026 Timothy Place and the catenae contributors
#include "catenae_c.h"

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

#include "async/junction.h"
#include "bridge/bridge.h"
#include "bridge/design.h"
#include "catenae/chain.h"
#include "catenae/delay.h"
#include "catenae/expected.h"
#include "catenae/gain.h"
#include "catenae/map.h"
#include "catenae/span.h"
#include "rational/chain.h"
#include "strip.h"

/// The types the manifest's entries name: type-level choices only (PLAN.md 7.5).
namespace catenae_c_types {

    using strip = example::strip_t;

    inline constexpr auto strip_mono_configuration =
        catenae::gain<1>{1.0f, 96} | catenae::delay<1, 32>{} | catenae::map<1, example::halve>{example::halve{}};
    using strip_mono = std::remove_cv_t<decltype(strip_mono_configuration)>;

    /// The rational skeleton's 3/2 converter, stereo.
    using rational_3_2 = catenae_fit::rational::converter<float, 2, catenae::ratio_t<3, 2>>;
    inline constexpr rational_3_2 rational_3_2_configuration = rational_3_2{};

    /// The bridge skeleton at 8 taps, stereo, in a chain so that it has process. Its shipped
    /// configuration runs a placeholder ROM table (PLAN.md 9.2's pattern), a triangle the
    /// compiler computes, each branch summing to 1; make() replaces it with a designed table in
    /// the caller's storage.
    using bridge_design = catenae_fit::bridge::design<float, 8>;
    using bridge_stage  = catenae_fit::bridge::bridge_to_48k<2, 8>;
    constexpr bridge_design::table_type placeholder_table() noexcept {
        bridge_design::table_type rom{};
        constexpr std::size_t     taps       = bridge_design::taps;
        constexpr double          centre     = static_cast<double>(catenae_fit::bridge::phases * taps - 1) / 2.0;
        constexpr double          half_width = 1.5 * static_cast<double>(catenae_fit::bridge::phases);
        for (std::size_t p = 0; p < bridge_design::stored_rows; ++p) {
            std::array<double, taps> row{};
            double                   sum = 0.0;
            for (std::size_t k = 0; k < taps; ++k) {
                const double j    = static_cast<double>(k * catenae_fit::bridge::phases + p);
                const double d    = j > centre ? j - centre : centre - j;
                row[taps - 1 - k] = d < half_width ? 1.0 - d / half_width : 0.0;
                sum += row[taps - 1 - k];
            }
            for (std::size_t t = 0; t < taps; ++t) {
                rom[p * taps + t] = static_cast<float>(row[t] / sum);
            }
        }
        return rom;
    }
    inline constexpr bridge_design::table_type bridge_placeholder = placeholder_table();
    inline constexpr auto bridge_48k_configuration                = catenae::chain(bridge_stage{bridge_placeholder});
    using bridge_48k = std::remove_cv_t<decltype(bridge_48k_configuration)>;

    /// The async skeleton, stereo, a 1024-frame ring fed 64 frames at a time.
    using async_2 = catenae_fit::async::junction<2, 1024, 64>;
    inline constexpr async_2 async_2_configuration{};

    /// Each entry's configuration as the library ships it, which configuration_copy constructs.
    template <typename T>
    struct shipped;
    template <>
    struct shipped<rational_3_2> {
        static constexpr const rational_3_2& value = rational_3_2_configuration;
    };
    template <>
    struct shipped<bridge_48k> {
        static constexpr const bridge_48k& value = bridge_48k_configuration;
    };
    template <>
    struct shipped<async_2> {
        static constexpr const async_2& value = async_2_configuration;
    };
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

    template <typename T>
    catenae_c_status junction_init(const void* configuration, std::size_t configuration_size, void* state,
                                   std::size_t state_size) noexcept {
        const T*           c = nullptr;
        typename T::state* s = nullptr;
        catenae_c_status   r = as_configuration(configuration, configuration_size, c);
        if (r == CATENAE_C_OK) {
            r = as_storage(state, state_size, s);
        }
        if (r == CATENAE_C_OK) {
            c->reset(*s);
        }
        return r;
    }

    template <typename T>
    catenae_c_status push(const void* configuration, std::size_t configuration_size, void* state,
                          std::size_t state_size, const float* in, std::size_t in_values,
                          std::size_t* accepted) noexcept {
        constexpr std::size_t ch = T::sink_port::values_per_frame;
        const T*              c  = nullptr;
        typename T::state*    s  = nullptr;
        catenae_c_status      r  = as_configuration(configuration, configuration_size, c);
        if (r == CATENAE_C_OK) {
            r = as_storage(state, state_size, s);
        }
        if (r != CATENAE_C_OK) {
            return r;
        }
        if ((in == nullptr && in_values != 0) || accepted == nullptr) {
            return CATENAE_C_NULL;
        }
        if (in_values % ch != 0) {
            return CATENAE_C_NOT_WHOLE_FRAMES;
        }
        if (in_values / ch > T::largest_push) {
            return CATENAE_C_WRONG_SIZE;
        }
        *accepted = c->push(*s, catenae::span<const float>(in, in_values)) * ch;
        return CATENAE_C_OK;
    }

    template <typename T>
    catenae_c_status pull(const void* configuration, std::size_t configuration_size, void* state,
                          std::size_t state_size, float* out, std::size_t out_values, std::size_t* real) noexcept {
        constexpr std::size_t ch = T::source_port::values_per_frame;
        const T*              c  = nullptr;
        typename T::state*    s  = nullptr;
        catenae_c_status      r  = as_configuration(configuration, configuration_size, c);
        if (r == CATENAE_C_OK) {
            r = as_storage(state, state_size, s);
        }
        if (r != CATENAE_C_OK) {
            return r;
        }
        if ((out == nullptr && out_values != 0) || real == nullptr) {
            return CATENAE_C_NULL;
        }
        if (out_values % ch != 0) {
            return CATENAE_C_NOT_WHOLE_FRAMES;
        }
        if (out_values / ch > T::largest_pull) {
            return CATENAE_C_WRONG_SIZE;
        }
        *real = c->pull(*s, catenae::span<float>(out, out_values)) * ch;
        return CATENAE_C_OK;
    }

    template <typename T>
    catenae_c_status report(const void* configuration, std::size_t configuration_size, const void* state,
                            std::size_t state_size, void* out, std::size_t out_size) noexcept {
        using report_t     = typename T::report_type;
        const T*         c = nullptr;
        catenae_c_status r = as_configuration(configuration, configuration_size, c);
        if (r != CATENAE_C_OK) {
            return r;
        }
        if (state == nullptr || out == nullptr) {
            return CATENAE_C_NULL;
        }
        if (state_size < sizeof(typename T::state) || out_size < sizeof(report_t)) {
            return CATENAE_C_WRONG_SIZE;
        }
        if (!aligned<typename T::state>(state) || !aligned<report_t>(out)) {
            return CATENAE_C_MISALIGNED;
        }
        ::new (out) report_t(c->report(*static_cast<const typename T::state*>(state)));
        return CATENAE_C_OK;
    }

    /// A make() whose expected failed, as the C status.
    catenae_c_status rejected(catenae::status why) noexcept {
        return why == catenae::status::size_mismatch ? CATENAE_C_WRONG_SIZE : CATENAE_C_REJECTED;
    }

    /// The bridge entry's design: the table into the caller's storage, the configuration over it.
    catenae_c_status make_bridge_48k(void* table, std::size_t table_size, void* scratch, std::size_t scratch_size,
                                     void* configuration, std::size_t configuration_size, double passband_hz,
                                     double stopband_atten_db) noexcept {
        using design_t = catenae_c_types::bridge_design;
        using coeff    = design_t::coeff;
        if (table == nullptr || scratch == nullptr || configuration == nullptr) {
            return CATENAE_C_NULL;
        }
        if (configuration_size < sizeof(catenae_c_types::bridge_48k)) {
            return CATENAE_C_WRONG_SIZE;
        }
        if (!aligned<coeff>(table) || !aligned<double>(scratch)
            || !aligned<catenae_c_types::bridge_48k>(configuration)) {
            return CATENAE_C_MISALIGNED;
        }
        const auto made =
            design_t::make(catenae_fit::bridge::spec{passband_hz, stopband_atten_db},
                           catenae::span<coeff>(static_cast<coeff*>(table), table_size / sizeof(coeff)),
                           catenae::span<double>(static_cast<double*>(scratch), scratch_size / sizeof(double)));
        if (!made) {
            return rejected(made.error());
        }
        ::new (configuration) catenae_c_types::bridge_48k(catenae::chain(catenae_c_types::bridge_stage{*made}));
        return CATENAE_C_OK;
    }

    /// The async entry's configuration from its target, the rest as configured by default.
    catenae_c_status make_async_2(void* configuration, std::size_t configuration_size,
                                  unsigned target_latency_frames) noexcept {
        using junction_t = catenae_c_types::async_2;
        if (configuration == nullptr) {
            return CATENAE_C_NULL;
        }
        if (configuration_size < sizeof(junction_t)) {
            return CATENAE_C_WRONG_SIZE;
        }
        if (!aligned<junction_t>(configuration)) {
            return CATENAE_C_MISALIGNED;
        }
        junction_t::config wanted{};
        wanted.target_latency_frames = target_latency_frames;
        const auto made              = junction_t::make(wanted);
        if (!made) {
            return rejected(made.error());
        }
        ::new (configuration) junction_t(*made);
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

#define CATENAE_C_CHECK_JUNCTION(name, ch, ccap, calign, scap, salign, rcap, ralign)                                   \
    static_assert(catenae_c_types::name::sink_port::values_per_frame == (ch), "manifest: " #name " channels");         \
    static_assert(sizeof(catenae_c_types::name) <= (ccap) && alignof(catenae_c_types::name) <= (calign),               \
                  "manifest: " #name "'s configuration exceeds its capacity on this target");                          \
    static_assert(sizeof(catenae_c_types::name::state) <= (scap) && alignof(catenae_c_types::name::state) <= (salign), \
                  "manifest: " #name "'s state exceeds its capacity on this target");                                  \
    static_assert(sizeof(catenae_c_types::name::report_type) <= (rcap)                                                 \
                      && alignof(catenae_c_types::name::report_type) <= (ralign),                                      \
                  "manifest: " #name "'s report exceeds its capacity on this target");
CATENAE_C_JUNCTIONS(CATENAE_C_CHECK_JUNCTION)
#undef CATENAE_C_CHECK_JUNCTION

static_assert(catenae_c_types::bridge_design::table_size() * sizeof(catenae_c_types::bridge_design::coeff)
                      <= catenae_c_bridge_48k_table_capacity
                  && catenae_c_types::bridge_design::workspace_size() * sizeof(double)
                         <= catenae_c_bridge_48k_scratch_capacity,
              "manifest: bridge_48k's table or scratch exceeds its capacity");

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

#define CATENAE_C_DEFINE_JUNCTION(name, ch, ccap, calign, scap, salign, rcap, ralign)                                  \
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
        return sizeof(catenae_c_types::name::state);                                                                   \
    }                                                                                                                  \
    size_t catenae_c_##name##_state_align(void) {                                                                      \
        return alignof(catenae_c_types::name::state);                                                                  \
    }                                                                                                                  \
    size_t catenae_c_##name##_report_size(void) {                                                                      \
        return sizeof(catenae_c_types::name::report_type);                                                             \
    }                                                                                                                  \
    catenae_c_status catenae_c_##name##_init(const void* configuration, size_t configuration_size, void* state,        \
                                             size_t state_size) {                                                      \
        return junction_init<catenae_c_types::name>(configuration, configuration_size, state, state_size);             \
    }                                                                                                                  \
    catenae_c_status catenae_c_##name##_push(const void* configuration, size_t configuration_size, void* state,        \
                                             size_t state_size, const float* in, size_t in_values, size_t* accepted) { \
        return push<catenae_c_types::name>(configuration, configuration_size, state, state_size, in, in_values,        \
                                           accepted);                                                                  \
    }                                                                                                                  \
    catenae_c_status catenae_c_##name##_pull(const void* configuration, size_t configuration_size, void* state,        \
                                             size_t state_size, float* out, size_t out_values, size_t* real) {         \
        return pull<catenae_c_types::name>(configuration, configuration_size, state, state_size, out, out_values,      \
                                           real);                                                                      \
    }                                                                                                                  \
    catenae_c_status catenae_c_##name##_report(const void* configuration, size_t configuration_size,                   \
                                               const void* state, size_t state_size, void* out, size_t out_size) {     \
        return report<catenae_c_types::name>(configuration, configuration_size, state, state_size, out, out_size);     \
    }
CATENAE_C_JUNCTIONS(CATENAE_C_DEFINE_JUNCTION)
#undef CATENAE_C_DEFINE_JUNCTION

#define CATENAE_C_DEFINE_DESIGN(name, tcap, talign, wcap, walign, params, args)                                        \
    size_t catenae_c_##name##_table_size(void) {                                                                       \
        return catenae_c_types::bridge_design::table_size() * sizeof(catenae_c_types::bridge_design::coeff);           \
    }                                                                                                                  \
    size_t catenae_c_##name##_scratch_size(void) {                                                                     \
        return catenae_c_types::bridge_design::workspace_size() * sizeof(double);                                      \
    }                                                                                                                  \
    catenae_c_status catenae_c_##name##_make(void* table, size_t table_size, void* scratch, size_t scratch_size,       \
                                             void* configuration, size_t configuration_size,                           \
                                             CATENAE_C_UNWRAP params) {                                                \
        return make_##name(table, table_size, scratch, scratch_size, configuration, configuration_size,                \
                           CATENAE_C_UNWRAP args);                                                                     \
    }
CATENAE_C_DESIGNS(CATENAE_C_DEFINE_DESIGN)
#undef CATENAE_C_DEFINE_DESIGN

#define CATENAE_C_DEFINE_MAKER(name, params, args)                                                                     \
    catenae_c_status catenae_c_##name##_make(void* configuration, size_t configuration_size,                           \
                                             CATENAE_C_UNWRAP params) {                                                \
        return make_##name(configuration, configuration_size, CATENAE_C_UNWRAP args);                                  \
    }
CATENAE_C_MAKERS(CATENAE_C_DEFINE_MAKER)
#undef CATENAE_C_DEFINE_MAKER

#define CATENAE_C_COUNT(...) +1
size_t catenae_c_entries(void) {
    return 0 CATENAE_C_MANIFEST(CATENAE_C_COUNT) CATENAE_C_JUNCTIONS(CATENAE_C_COUNT);
}
// Nine functions a chain entry, ten a junction, three a design, one a maker or a setter, and
// these two.
size_t catenae_c_functions(void) {
    return 9 * (0 CATENAE_C_MANIFEST(CATENAE_C_COUNT)) + 10 * (0 CATENAE_C_JUNCTIONS(CATENAE_C_COUNT))
           + 3 * (0 CATENAE_C_DESIGNS(CATENAE_C_COUNT)) + (0 CATENAE_C_MAKERS(CATENAE_C_COUNT))
           + (0 CATENAE_C_PARAMETERS(CATENAE_C_COUNT)) + 2;
}
#undef CATENAE_C_COUNT

} // extern "C"
