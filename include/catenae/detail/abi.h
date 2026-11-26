/// @file abi.h
/// @brief The inline namespace that carries this build's configuration (PLAN.md section 3.4).
///
/// @req{CAT-ABI-001} A file built at one level that calls the compiled core does not link
/// against a core built at the other; one whose checks are all evaluated while it compiles
/// links against either, or against nothing.
/// @req{CAT-ABI-002} The inline namespace is the level, then the FFT back end of float, double,
/// q15 and q31, joined by underscores (`boundary_own_own_own_own`), so a file that calls the
/// compiled core does not link against a core built with another FFT choice.
// Copyright 2026 Timothy Place and the catenae contributors
//
// Every declaration of catenae lives in `catenae::CATENAE_ABI`, an inline namespace spelled
// from catenae/config.h: the check level, `boundary` or `audit` (D27), and each sample type's
// FFT back end, `own`, `cmsis`, `vdsp` or `none` (D28). A name that differs by configuration has
// a different mangled name in each, so a file that calls the compiled core or holds a run-time
// check does not link against another configuration, and nothing is shared across them. A file
// whose checks are all compile-time references nothing and links anywhere. A consumer
// forward-declares a catenae name only through catenae/fwd.h, never by writing
// `namespace catenae { ... }` itself.
#ifndef CATENAE_DETAIL_ABI_H
#define CATENAE_DETAIL_ABI_H

#include "catenae/config.h"

#if !defined(CATENAE_CHECKS) || !defined(CATENAE_CHECKS_BOUNDARY) || !defined(CATENAE_CHECKS_AUDIT)
#error "catenae: catenae/config.h does not name a check level; it is written by the build"
#endif
#if !defined(CATENAE_FFT_F32) || !defined(CATENAE_FFT_F64) || !defined(CATENAE_FFT_Q15) || !defined(CATENAE_FFT_Q31)
#error "catenae: catenae/config.h does not name the FFT back ends; it is written by the build"
#endif

#if CATENAE_CHECKS == CATENAE_CHECKS_BOUNDARY
#define CATENAE_DETAIL_ABI_LEVEL boundary
#elif CATENAE_CHECKS == CATENAE_CHECKS_AUDIT
#define CATENAE_DETAIL_ABI_LEVEL audit
#else
#error "catenae: CATENAE_CHECKS is CATENAE_CHECKS_BOUNDARY or CATENAE_CHECKS_AUDIT"
#endif

// Each type's back end as a word of the namespace.
#if CATENAE_FFT_F32 == CATENAE_FFT_OWN
#define CATENAE_DETAIL_ABI_F32 own
#elif CATENAE_FFT_F32 == CATENAE_FFT_CMSIS
#define CATENAE_DETAIL_ABI_F32 cmsis
#elif CATENAE_FFT_F32 == CATENAE_FFT_VDSP
#define CATENAE_DETAIL_ABI_F32 vdsp
#elif CATENAE_FFT_F32 == CATENAE_FFT_NONE
#define CATENAE_DETAIL_ABI_F32 none
#else
#error "catenae: CATENAE_FFT_F32 is own, cmsis, vdsp or none"
#endif

#if CATENAE_FFT_F64 == CATENAE_FFT_OWN
#define CATENAE_DETAIL_ABI_F64 own
#elif CATENAE_FFT_F64 == CATENAE_FFT_VDSP
#define CATENAE_DETAIL_ABI_F64 vdsp
#elif CATENAE_FFT_F64 == CATENAE_FFT_NONE
#define CATENAE_DETAIL_ABI_F64 none
#else
#error "catenae: CATENAE_FFT_F64 is own, vdsp or none"
#endif

#if CATENAE_FFT_Q15 == CATENAE_FFT_OWN
#define CATENAE_DETAIL_ABI_Q15 own
#elif CATENAE_FFT_Q15 == CATENAE_FFT_CMSIS
#define CATENAE_DETAIL_ABI_Q15 cmsis
#elif CATENAE_FFT_Q15 == CATENAE_FFT_NONE
#define CATENAE_DETAIL_ABI_Q15 none
#else
#error "catenae: CATENAE_FFT_Q15 is own, cmsis or none"
#endif

#if CATENAE_FFT_Q31 == CATENAE_FFT_OWN
#define CATENAE_DETAIL_ABI_Q31 own
#elif CATENAE_FFT_Q31 == CATENAE_FFT_CMSIS
#define CATENAE_DETAIL_ABI_Q31 cmsis
#elif CATENAE_FFT_Q31 == CATENAE_FFT_NONE
#define CATENAE_DETAIL_ABI_Q31 none
#else
#error "catenae: CATENAE_FFT_Q31 is own, cmsis or none"
#endif

#define CATENAE_DETAIL_ABI_JOIN2(l, a, b, c, d) l##_##a##_##b##_##c##_##d
#define CATENAE_DETAIL_ABI_JOIN(l, a, b, c, d) CATENAE_DETAIL_ABI_JOIN2(l, a, b, c, d)

/// The inline namespace of this build: its check level and its four FFT back ends.
#define CATENAE_ABI                                                                                                    \
    CATENAE_DETAIL_ABI_JOIN(CATENAE_DETAIL_ABI_LEVEL, CATENAE_DETAIL_ABI_F32, CATENAE_DETAIL_ABI_F64,                  \
                            CATENAE_DETAIL_ABI_Q15, CATENAE_DETAIL_ABI_Q31)

#endif // CATENAE_DETAIL_ABI_H
