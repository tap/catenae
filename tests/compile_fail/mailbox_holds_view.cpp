// Copyright 2026 Timothy Place and the catenae contributors
// A mailbox of a chain with a stage that holds its table by view: the chain holds a view, and
// a table design is replaced through an acknowledgement instead (PLAN.md 6.3).
// Verifies CAT-MAILBOX-004.
// Expect: catenae: a mailbox carries a value that owns what it refers to, not one that holds a view
#include "catenae/chain.h"
#include "catenae/gain.h"
#include "catenae/mailbox.h"
#include "catenae_fixtures/stages.h"

#ifdef CATENAE_CF_CONTROL
using posted_t = catenae::chain<catenae::gain<2>, catenae::gain<2>>;
#else
using posted_t = catenae::chain<catenae::gain<2>, catenae_fixtures::table_gain<2>>;
#endif

catenae::mailbox<posted_t>& levels() noexcept;
catenae::mailbox<posted_t>& levels() noexcept {
    static catenae::mailbox<posted_t> box;
    return box;
}
