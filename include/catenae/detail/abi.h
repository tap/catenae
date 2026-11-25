/// @file abi.h
/// @brief The inline namespace that carries this build's configuration (PLAN.md section 3.4).
///
/// @req{CAT-ABI-001} A file built at one level that calls the compiled core does not link
/// against a core built at the other; one whose checks are all evaluated while it compiles
/// links against either, or against nothing.
// Copyright 2026 Timothy Place and the catenae contributors
//
// Every declaration of catenae lives in `catenae::CATENAE_ABI`, an inline namespace spelled
// from catenae/config.h: `boundary` or `audit` (D27). A name that differs by level has a
// different mangled name at each, so a file that calls the compiled core or holds a run-time
// check does not link against another level, and nothing is shared across configurations.
// A file whose checks are all compile-time references nothing and links anywhere. The FFT
// choices join the spelling at C4. A consumer forward-declares a catenae name only through
// catenae/fwd.h, never by writing `namespace catenae { ... }` itself.
#ifndef CATENAE_DETAIL_ABI_H
#define CATENAE_DETAIL_ABI_H

#include "catenae/config.h"

#if !defined(CATENAE_CHECKS) || !defined(CATENAE_CHECKS_BOUNDARY) || !defined(CATENAE_CHECKS_AUDIT)
#error "catenae: catenae/config.h does not name a check level; it is written by the build"
#endif

#if CATENAE_CHECKS == CATENAE_CHECKS_BOUNDARY
/// The inline namespace of this build: its check level.
#define CATENAE_ABI boundary
#elif CATENAE_CHECKS == CATENAE_CHECKS_AUDIT
#define CATENAE_ABI audit
#else
#error "catenae: CATENAE_CHECKS is CATENAE_CHECKS_BOUNDARY or CATENAE_CHECKS_AUDIT"
#endif

#endif // CATENAE_DETAIL_ABI_H
