/// @file test_dynamics_design.cpp
/// @brief The dynamics designs' contract (include/catenae/dynamics_design.h).
// Copyright 2026 Timothy Place and the catenae contributors
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

#include <gtest/gtest.h>

#include "catenae/check.h"
#include "catenae/dynamics_design.h"
#include "catenae_test/check_trap.h"
#include "dynamics_tables.h"

namespace {

    template <typename T>
    class dynamics_design_formats : public ::testing::Test {};
    using formats = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(dynamics_design_formats, formats, );

    using catenae::status;

    constexpr double k_np  = 0.11512925464970228420089957273422; // ln 10 / 20
    const double     k_inf = std::numeric_limits<double>::infinity();
    const double     k_nan = std::numeric_limits<double>::quiet_NaN();

    catenae::compressor_spec comp(double t, double r, double w, double a, double rel, double m) {
        return {t, r, w, a, rel, m};
    }
    catenae::expander_spec expd(double t, double r, double w, double range, double a, std::uint32_t h, double rel) {
        return {t, r, w, range, a, h, rel};
    }

    // Verifies CAT-DYNAMICS-DESIGN-001.
    TEST(dynamics_design, TheNumbersAreTheSpecsInNepers) {
        const auto c = catenae::compressor_design<double>::make(comp(-20.0, 4.0, 6.0, 10.0, 1000.0, 3.0));
        ASSERT_TRUE(c.has_value());
        const auto& n = c->numbers();
        EXPECT_DOUBLE_EQ(n.threshold, -20.0 * k_np);
        EXPECT_DOUBLE_EQ(n.half_knee, 3.0 * k_np);
        EXPECT_DOUBLE_EQ(n.above, 0.75);
        EXPECT_DOUBLE_EQ(n.knee_scale, 0.75 / (2.0 * 6.0 * k_np));
        EXPECT_DOUBLE_EQ(n.makeup, 3.0 * k_np);
        EXPECT_DOUBLE_EQ(n.attack, std::exp(-0.1));
        EXPECT_DOUBLE_EQ(n.attack_complement, -std::expm1(-0.1));
        EXPECT_DOUBLE_EQ(n.release, std::exp(-1e-3));
        EXPECT_DOUBLE_EQ(n.release_complement, -std::expm1(-1e-3));
        // A hard knee scales nothing, a ratio of infinity reduces neper for neper, and a time
        // constant of 0 is instant.
        const auto hard = catenae::compressor_design<double>::make(comp(-6.0, k_inf, 0.0, 0.0, 0.0, 0.0));
        ASSERT_TRUE(hard.has_value());
        EXPECT_EQ(hard->numbers().above, 1.0);
        EXPECT_EQ(hard->numbers().knee_scale, 0.0);
        EXPECT_EQ(hard->numbers().attack, 0.0);
        EXPECT_EQ(hard->numbers().attack_complement, 1.0);

        const auto l = catenae::limiter_design<double>::make({-1.0, 500.0});
        ASSERT_TRUE(l.has_value());
        EXPECT_DOUBLE_EQ(l->numbers().threshold, -1.0 * k_np);
        EXPECT_DOUBLE_EQ(l->numbers().ceiling, std::pow(10.0, -1.0 / 20.0));
        EXPECT_DOUBLE_EQ(l->numbers().release, std::exp(-1.0 / 500.0));

        const auto e = catenae::expander_design<double>::make(expd(-50.0, 3.0, 4.0, 60.0, 5.0, 480, 2000.0));
        ASSERT_TRUE(e.has_value());
        const auto& x = e->numbers();
        EXPECT_DOUBLE_EQ(x.threshold, -50.0 * k_np);
        EXPECT_DOUBLE_EQ(x.half_knee, 2.0 * k_np);
        EXPECT_DOUBLE_EQ(x.below, 2.0);
        EXPECT_DOUBLE_EQ(x.knee_scale, 2.0 / (2.0 * 4.0 * k_np));
        EXPECT_DOUBLE_EQ(x.range, 60.0 * k_np);
        EXPECT_EQ(x.hold, 480u);
        const auto gate = catenae::expander_design<double>::make(expd(-50.0, k_inf, 0.0, 80.0, 1.0, 0, 100.0));
        ASSERT_TRUE(gate.has_value());
        EXPECT_EQ(gate->numbers().below, k_inf);
    }

    // Verifies CAT-DYNAMICS-DESIGN-001.
    TEST(dynamics_design, FloatIsDoubleRounded) {
        const catenae::compressor_spec cs = comp(-18.0, 3.0, 8.0, 25.0, 4000.0, 2.0);
        const auto                     cd = catenae::compressor_design<double>::make(cs);
        const auto                     cf = catenae::compressor_design<float>::make(cs);
        ASSERT_TRUE(cd.has_value() && cf.has_value());
        EXPECT_EQ(cf->numbers().threshold, static_cast<float>(cd->numbers().threshold));
        EXPECT_EQ(cf->numbers().knee_scale, static_cast<float>(cd->numbers().knee_scale));
        EXPECT_EQ(cf->numbers().release, static_cast<float>(cd->numbers().release));
        EXPECT_EQ(cf->numbers().release_complement, static_cast<float>(cd->numbers().release_complement));
        const auto ld = catenae::limiter_design<double>::make({-0.3, 100.0});
        const auto lf = catenae::limiter_design<float>::make({-0.3, 100.0});
        ASSERT_TRUE(ld.has_value() && lf.has_value());
        EXPECT_EQ(lf->numbers().ceiling, static_cast<float>(ld->numbers().ceiling));
    }

    /// Whether b is within a float ulp of a.
    bool within_an_ulp(float a, float b) {
        return b >= std::nextafter(a, -std::numeric_limits<float>::infinity())
               && b <= std::nextafter(a, std::numeric_limits<float>::infinity());
    }

    // Verifies CAT-DYNAMICS-DESIGN-003.
    TEST(dynamics_design, ATableDesignIsWhatMakeGives) {
        // examples/dynamics_tables.h, constants: from make() on the hosted Linux leg, bit for bit;
        // within a float ulp of make() wherever another libm rounds the double design otherwise.
        const auto e = catenae::expander_design<float>::make({-50.0, 4.0, 6.0, 40.0, 48.0, 2400, 4800.0});
        const auto c = catenae::compressor_design<float>::make({-18.0, 4.0, 6.0, 240.0, 4800.0, 6.0});
        const auto l = catenae::limiter_design<float>::make({-1.0, 2400.0});
        ASSERT_TRUE(e && c && l);
        const auto& et = example::gate_design.numbers();
        const auto& ct = example::bus_design.numbers();
        const auto& lt = example::ceiling_design.numbers();
        for (const auto& [table, made] : {std::pair<float, float>{et.threshold, e->numbers().threshold},
                                          {et.half_knee, e->numbers().half_knee},
                                          {et.below, e->numbers().below},
                                          {et.knee_scale, e->numbers().knee_scale},
                                          {et.range, e->numbers().range},
                                          {et.attack, e->numbers().attack},
                                          {et.attack_complement, e->numbers().attack_complement},
                                          {et.release, e->numbers().release},
                                          {et.release_complement, e->numbers().release_complement},
                                          {ct.threshold, c->numbers().threshold},
                                          {ct.half_knee, c->numbers().half_knee},
                                          {ct.above, c->numbers().above},
                                          {ct.knee_scale, c->numbers().knee_scale},
                                          {ct.makeup, c->numbers().makeup},
                                          {ct.attack, c->numbers().attack},
                                          {ct.attack_complement, c->numbers().attack_complement},
                                          {ct.release, c->numbers().release},
                                          {ct.release_complement, c->numbers().release_complement},
                                          {lt.threshold, l->numbers().threshold},
                                          {lt.ceiling, l->numbers().ceiling},
                                          {lt.release, l->numbers().release},
                                          {lt.release_complement, l->numbers().release_complement}}) {
            EXPECT_TRUE(within_an_ulp(made, table)) << made << " and the table's " << table;
        }
        EXPECT_EQ(et.hold, e->numbers().hold);
    }

    // Verifies CAT-DYNAMICS-DESIGN-002.
    TYPED_TEST(dynamics_design_formats, MakeRejectsWhatItsCardNames) {
        using cd             = catenae::compressor_design<TypeParam>;
        using ld             = catenae::limiter_design<TypeParam>;
        using ed             = catenae::expander_design<TypeParam>;
        const auto status_of = [](const auto& made) { return made ? 0 : static_cast<int>(made.error()); };
        const int  invalid   = static_cast<int>(status::invalid_argument);
        const int  range     = static_cast<int>(status::out_of_range);
        // The compressor's.
        EXPECT_EQ(status_of(cd::make(comp(k_nan, 2.0, 0.0, 1.0, 1.0, 0.0))), invalid);
        EXPECT_EQ(status_of(cd::make(comp(-k_inf, 2.0, 0.0, 1.0, 1.0, 0.0))), invalid);
        EXPECT_EQ(status_of(cd::make(comp(-10.0, 0.5, 0.0, 1.0, 1.0, 0.0))), invalid);
        EXPECT_EQ(status_of(cd::make(comp(-10.0, k_nan, 0.0, 1.0, 1.0, 0.0))), invalid);
        EXPECT_EQ(status_of(cd::make(comp(-10.0, 2.0, -1.0, 1.0, 1.0, 0.0))), invalid);
        EXPECT_EQ(status_of(cd::make(comp(-10.0, 2.0, k_inf, 1.0, 1.0, 0.0))), invalid);
        EXPECT_EQ(status_of(cd::make(comp(-10.0, 2.0, 0.0, -1.0, 1.0, 0.0))), invalid);
        EXPECT_EQ(status_of(cd::make(comp(-10.0, 2.0, 0.0, 1.0, k_inf, 0.0))), invalid);
        EXPECT_EQ(status_of(cd::make(comp(-10.0, 2.0, 0.0, 1.0, 1.0, k_nan))), invalid);
        EXPECT_EQ(status_of(cd::make(comp(-200.5, 2.0, 0.0, 1.0, 1.0, 0.0))), range);
        EXPECT_EQ(status_of(cd::make(comp(-10.0, 2.0, 200.5, 1.0, 1.0, 0.0))), range);
        EXPECT_EQ(status_of(cd::make(comp(-10.0, 2.0, 0.0, 1.0, 1.0, 200.5))), range);
        // A time constant whose coefficient rounds to 1 in S.
        const double too_long = std::is_same<TypeParam, float>::value ? 1e9 : 1e17;
        EXPECT_EQ(status_of(cd::make(comp(-10.0, 2.0, 0.0, too_long, 1.0, 0.0))), range);
        // The bounds are accepted.
        EXPECT_EQ(status_of(cd::make(comp(-200.0, 1.0, 200.0, 0.0, 0.0, 200.0))), 0);
        EXPECT_EQ(status_of(cd::make(comp(200.0, k_inf, 0.0, 1e6, 1e6, -200.0))), 0);
        // The limiter's.
        EXPECT_EQ(status_of(ld::make({k_nan, 1.0})), invalid);
        EXPECT_EQ(status_of(ld::make({-1.0, -1.0})), invalid);
        EXPECT_EQ(status_of(ld::make({-1.0, k_nan})), invalid);
        EXPECT_EQ(status_of(ld::make({200.5, 1.0})), range);
        EXPECT_EQ(status_of(ld::make({-200.0, 0.0})), 0);
        // The expander's.
        EXPECT_EQ(status_of(ed::make(expd(-50.0, 0.9, 0.0, 40.0, 1.0, 0, 1.0))), invalid);
        EXPECT_EQ(status_of(ed::make(expd(-50.0, 2.0, 0.0, 0.0, 1.0, 0, 1.0))), invalid);
        EXPECT_EQ(status_of(ed::make(expd(-50.0, 2.0, 0.0, -3.0, 1.0, 0, 1.0))), invalid);
        EXPECT_EQ(status_of(ed::make(expd(-50.0, 2.0, 0.0, k_inf, 1.0, 0, 1.0))), invalid);
        EXPECT_EQ(status_of(ed::make(expd(-50.0, 2.0, 0.0, 40.0, k_nan, 0, 1.0))), invalid);
        EXPECT_EQ(status_of(ed::make(expd(-50.0, 2.0, 0.0, 200.5, 1.0, 0, 1.0))), range);
        EXPECT_EQ(status_of(ed::make(expd(-50.0, k_inf, 6.0, 200.0, 0.0, 0xFFFFFFFFu, 0.0))), 0);
    }

    // Verifies CAT-DYNAMICS-DESIGN-003.
    TYPED_TEST(dynamics_design_formats, FromTableAcceptsItsOwnNumbersBitForBit) {
        using cd     = catenae::compressor_design<TypeParam>;
        const auto c = cd::make(comp(-24.0, 2.5, 10.0, 3.0, 300.0, 6.0));
        ASSERT_TRUE(c.has_value());
        const cd again = cd::from_table(c->numbers());
        EXPECT_EQ(std::memcmp(&again, &*c, sizeof(cd)), 0);
        using ld      = catenae::limiter_design<TypeParam>;
        const auto l  = ld::make({-1.0, 64.0});
        const ld   lt = ld::from_table(l->numbers());
        EXPECT_EQ(std::memcmp(&lt, &*l, sizeof(ld)), 0);
        using ed      = catenae::expander_design<TypeParam>;
        const auto e  = ed::make(expd(-45.0, k_inf, 0.0, 70.0, 2.0, 100, 900.0));
        const ed   et = ed::from_table(e->numbers());
        // Field by field: in double the numbers end in padding after the hold.
        const auto& a = et.numbers();
        const auto& b = e->numbers();
        EXPECT_EQ(a.threshold, b.threshold);
        EXPECT_EQ(a.half_knee, b.half_knee);
        EXPECT_EQ(a.below, b.below);
        EXPECT_EQ(a.knee_scale, b.knee_scale);
        EXPECT_EQ(a.range, b.range);
        EXPECT_EQ(a.attack, b.attack);
        EXPECT_EQ(a.attack_complement, b.attack_complement);
        EXPECT_EQ(a.release, b.release);
        EXPECT_EQ(a.release_complement, b.release_complement);
        EXPECT_EQ(a.hold, b.hold);
    }

    // Verifies CAT-DYNAMICS-DESIGN-003.
    TYPED_TEST(dynamics_design_formats, FromTableRefusesNumbersOutOfRange) {
        using cd = catenae::compressor_design<TypeParam>;
        auto n   = cd::make(comp(-24.0, 2.5, 10.0, 3.0, 300.0, 6.0))->numbers();
        n.attack = TypeParam{1};
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected, static_cast<void>(cd::from_table(n)));
        n.attack = TypeParam{0.5};
        n.above  = TypeParam{1.5};
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected, static_cast<void>(cd::from_table(n)));
        n.above     = TypeParam{0.5};
        n.half_knee = -TypeParam{1};
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected, static_cast<void>(cd::from_table(n)));
        using ld  = catenae::limiter_design<TypeParam>;
        auto l    = ld::make({-1.0, 64.0})->numbers();
        l.ceiling = TypeParam{0};
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected, static_cast<void>(ld::from_table(l)));
        using ed = catenae::expander_design<TypeParam>;
        auto e   = ed::make(expd(-45.0, 2.0, 0.0, 70.0, 2.0, 100, 900.0))->numbers();
        e.range  = std::numeric_limits<TypeParam>::infinity();
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected, static_cast<void>(ed::from_table(e)));
        e.range = TypeParam{1};
        e.below = std::numeric_limits<TypeParam>::quiet_NaN();
        CATENAE_EXPECT_CHECK(catenae::check::table_rejected, static_cast<void>(ed::from_table(e)));
    }

} // namespace
