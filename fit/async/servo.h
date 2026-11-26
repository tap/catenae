/// @file servo.h
/// @brief The async skeleton's clock servo: SampleRateTap's type-2 PI loop and its three-stage
/// lock machine, split into a configuration and a state (CF).
///
/// | Contract  |                                                                                  |
/// |-----------|----------------------------------------------------------------------------------|
/// | Plant     | the elastic ring: d(occ)/dt = fs (eps - eps_hat), a pure integrator of gain fs  |
/// | Loop      | eps_hat = Kp e + Ki integral(e dt); Kp = 2 zeta wn / fs, Ki = wn^2 / fs          |
/// | Stages    | acquire (10 Hz), track (1 Hz), quiet (0.05 Hz), each promotion held and averaged |
/// | State     | `servo_state`: plain storage; all zero is acquire with nothing integrated      |
/// | Numbers   | `double`, and `std::exp` on every update: carried as it is (PLAN.md section 12) |
///
/// Carried from SampleRateTap 4496888, async/include/tap/sr/async/pi_servo.h: `servo_config`
/// whole (less `scaled_to`), and `pi_servo`'s arithmetic unchanged. What changed is the shape:
/// the constants and the gains are the configuration, a `constexpr` value; what the loop
/// remembers is `servo_state`, which the junction's state holds; the setpoint is an argument
/// (the junction's, which its pulls may raise), not a member. `hold_timer == 0.0` is written
/// `hold_timer <= 0.0` (the timer is never negative), since `==` on a double is an error under
/// catenae's warning sets (-Wfloat-equal).
// Copyright 2026 Timothy Place and the catenae contributors
#ifndef CATENAE_FIT_ASYNC_SERVO_H
#define CATENAE_FIT_ASYNC_SERVO_H

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "async/bank.h"

namespace catenae_fit {
    namespace async {

        /// The servo's tuning: SampleRateTap's defaults, for a 48 kHz near-unity converter.
        struct servo_config {
            double acquire_bandwidth_hz    = 10.0;   ///< stage-1 loop bandwidth
            double track_bandwidth_hz      = 1.0;    ///< stage-2 loop bandwidth
            double quiet_bandwidth_hz      = 0.05;   ///< stage-3 loop bandwidth
            double damping                 = 1.0;    ///< zeta; 1.0 = critically damped
            double acquire_smoother_hz     = 50.0;   ///< one-pole error prefilter, acquire
            double track_smoother_hz       = 5.0;    ///< one-pole error prefilter, track
            double quiet_smoother_hz       = 0.5;    ///< 3-pole cascade corner (always runs)
            double lock_threshold_frames   = 1.0;    ///< |e| below this ...
            double lock_hold_seconds       = 0.5;    ///< ... this long => acquire -> track
            double quiet_hold_seconds      = 2.0;    ///< cascade-|e| hold => track -> quiet
            double unlock_threshold_frames = 24.0;   ///< |e| above this => demote a stage
            double max_deviation_ppm       = 1000.0; ///< eps_hat clamp = +/- 1.5x this
        };

        /// The servo's stage, as a 32-bit word of its state.
        enum class lock_stage : std::uint32_t {
            acquire, ///< wide: pulling in (zero, so a state at rest acquires)
            track,   ///< locked, following the block beat
            quiet,   ///< locked, narrow
        };

        /// What the servo remembers: plain storage, at rest all zero. Seeded before its first update.
        struct servo_state {
            double     lp_fast;    ///< acquire/track error smoother
            double     q1;         ///< quiet three-pole cascade, first pole
            double     q2;         ///< second pole
            double     q3;         ///< third pole
            double     integ;      ///< the integrator: the ppm estimate's slow part
            double     eps_hat;    ///< the latest estimate
            double     eps_avg;    ///< the hold window's average of the estimate
            double     hold_timer; ///< seconds the error has stayed below the threshold
            lock_stage stage;      ///< acquire, track or quiet
        };

        /// @brief The PI loop filter and its lock machine: a configuration, constant while it runs.
        class pi_servo {
          public:
            /// The loop for `cfg` at `sample_rate_hz`: its three pairs of gains computed here.
            constexpr pi_servo(const servo_config& cfg, double sample_rate_hz) noexcept
                : m_cfg(cfg)
                , m_kp_acquire(kp_for(cfg.acquire_bandwidth_hz, cfg.damping, sample_rate_hz))
                , m_ki_acquire(ki_for(cfg.acquire_bandwidth_hz, sample_rate_hz))
                , m_kp_track(kp_for(cfg.track_bandwidth_hz, cfg.damping, sample_rate_hz))
                , m_ki_track(ki_for(cfg.track_bandwidth_hz, sample_rate_hz))
                , m_kp_quiet(kp_for(cfg.quiet_bandwidth_hz, cfg.damping, sample_rate_hz))
                , m_ki_quiet(ki_for(cfg.quiet_bandwidth_hz, sample_rate_hz)) {}

            /// Re-arm the loop at `target`. `keep_integrator` keeps the ppm estimate (after a
            /// dropout: the clocks have not changed).
            void reset(servo_state& st, bool keep_integrator, double target) const noexcept {
                if (!keep_integrator) {
                    st.integ = 0.0;
                }
                st.eps_hat = st.integ;
                seed(st, target);
                st.stage      = lock_stage::acquire;
                st.hold_timer = 0.0;
            }

            /// Seed the error smoothers when the observable jumps for a known reason.
            static void seed(servo_state& st, double occ_plus_mu) noexcept {
                st.lp_fast = occ_plus_mu;
                st.q1      = occ_plus_mu;
                st.q2      = occ_plus_mu;
                st.q3      = occ_plus_mu;
            }

            /// One control update, once a pull before synthesis: the backlog `occ_frames`, the
            /// resampler's fractional position `mu`, the seconds `dt` the pull covers, and the
            /// setpoint `target`. Returns eps_hat, the rate-deviation estimate.
            double update(servo_state& st, double target, double occ_frames, double mu, double dt) const noexcept {
                const double meas = occ_frames + mu;
                const double fast_hz =
                    st.stage == lock_stage::acquire ? m_cfg.acquire_smoother_hz : m_cfg.track_smoother_hz;
                st.lp_fast += alpha(fast_hz, dt) * (meas - st.lp_fast);
                const double aq = alpha(m_cfg.quiet_smoother_hz, dt);
                st.q1 += aq * (meas - st.q1);
                st.q2 += aq * (st.q1 - st.q2);
                st.q3 += aq * (st.q2 - st.q3);
                const double e_fast  = st.lp_fast - target;
                const double e_quiet = st.q3 - target;
                const double limit   = 1.5 * m_cfg.max_deviation_ppm * 1e-6;
                advance_stage(st, e_fast, e_quiet, limit, dt);

                double kp = m_kp_quiet;
                double ki = m_ki_quiet;
                double e  = e_quiet;
                if (st.stage == lock_stage::acquire) {
                    kp = m_kp_acquire;
                    ki = m_ki_acquire;
                    e  = e_fast;
                }
                else if (st.stage == lock_stage::track) {
                    kp = m_kp_track;
                    ki = m_ki_track;
                    e  = e_fast;
                }
                st.integ   = std::clamp(st.integ + ki * e * dt, -limit, limit); // anti-windup
                st.eps_hat = std::clamp(kp * e + st.integ, -limit, limit);
                return st.eps_hat;
            }

            /// Whether the loop has left acquisition.
            static bool locked(const servo_state& st) noexcept { return st.stage != lock_stage::acquire; }
            /// The smoothed occupancy the current stage reads.
            static double smoothed_occupancy(const servo_state& st) noexcept {
                return st.stage == lock_stage::quiet ? st.q3 : st.lp_fast;
            }
            /// The largest deviation it tracks, in ppm.
            constexpr double max_deviation_ppm() const noexcept { return m_cfg.max_deviation_ppm; }

          private:
            static constexpr double kp_for(double bandwidth_hz, double damping, double fs) noexcept {
                return 2.0 * damping * (2.0 * detail::k_pi * bandwidth_hz) / fs;
            }
            static constexpr double ki_for(double bandwidth_hz, double fs) noexcept {
                const double wn = 2.0 * detail::k_pi * bandwidth_hz;
                return wn * wn / fs;
            }
            static double alpha(double corner_hz, double dt) noexcept {
                return 1.0 - std::exp(-2.0 * detail::k_pi * corner_hz * dt);
            }

            /// The promotions and demotions of pi_servo::update, in its order.
            void advance_stage(servo_state& st, double e_fast, double e_quiet, double limit, double dt) const noexcept {
                switch (st.stage) {
                case lock_stage::acquire:
                    if (advance_hold(st, e_fast, m_cfg.lock_threshold_frames, m_cfg.lock_hold_seconds, dt)) {
                        st.stage = lock_stage::track;
                        st.integ = std::clamp(st.eps_avg, -limit, limit);
                    }
                    break;
                case lock_stage::track:
                    if (std::abs(e_fast) > m_cfg.unlock_threshold_frames) {
                        st.stage      = lock_stage::acquire;
                        st.hold_timer = 0.0;
                    }
                    else if (advance_hold(st, e_quiet, m_cfg.lock_threshold_frames, m_cfg.quiet_hold_seconds, dt)) {
                        st.stage = lock_stage::quiet;
                        st.integ = std::clamp(st.eps_avg, -limit, limit);
                    }
                    break;
                case lock_stage::quiet:
                    if (std::abs(e_quiet) > m_cfg.unlock_threshold_frames) {
                        st.stage      = lock_stage::track;
                        st.hold_timer = 0.0;
                    }
                    break;
                }
            }

            /// |e| below the threshold for `hold_seconds`, while eps_hat is averaged (time constant
            /// hold_seconds / 5) for the narrower stage's integrator.
            static bool advance_hold(servo_state& st, double e, double threshold, double hold_seconds,
                                     double dt) noexcept {
                if (std::abs(e) >= threshold) {
                    st.hold_timer = 0.0;
                    return false;
                }
                if (st.hold_timer <= 0.0) {
                    st.eps_avg = st.eps_hat;
                }
                else {
                    st.eps_avg += (1.0 - std::exp(-5.0 * dt / hold_seconds)) * (st.eps_hat - st.eps_avg);
                }
                st.hold_timer += dt;
                if (st.hold_timer < hold_seconds) {
                    return false;
                }
                st.hold_timer = 0.0;
                return true;
            }

            servo_config m_cfg;
            double       m_kp_acquire;
            double       m_ki_acquire;
            double       m_kp_track;
            double       m_ki_track;
            double       m_kp_quiet;
            double       m_ki_quiet;
        };

    } // namespace async
} // namespace catenae_fit

#endif // CATENAE_FIT_ASYNC_SERVO_H
