<!-- Copyright 2026 Timothy Place and the catenae contributors -->

# SampleRateTap's public members in catenae (CF)

This table was made from SampleRateTap at `4496888e187dfe094e3e02537bc4a8dcb5ecb028` (the commit `NOTICE.md` pins), with the members its classes inherit from DspTap read at the engines' submodule pin, `4b30a02` (DspTap 0.5.0), from a DspTap clone because the submodule is not checked out; and from catenae's `PLAN.md` and its contract pages (`docs/contracts/stage.md`, `chain.md`, `junction.md`) as they stand. A member is public when a consumer of the engine's umbrella header (`tap/sr/async/async.h`, `tap/sr/bridge/ratio.h`, `tap/sr/rational/rational.h`) or of its C ABI header (`<engine>/capi/tap_sr_<engine>_capi.h`) can name it: every type, alias, concept, function, public member function (deleted ones included), data member, constant, enumerator and macro outside a `detail` namespace, including the members an engine's class inherits from a DspTap base (marked "from" or "inherited"), which are the call shapes the READMEs document for `basic_chain` and the cascades. Protected and private members, include guards, a `using`-declaration that re-exports a base's member already listed, and the `#error` trap for the retired `SRT_CP_MIN_CHANNELS` are left out. Overloads are one row, and so are a C function's `_q15` and `_q31` twins, which are C's spelling of overloads; a family of named profile constants is one row where all share one fate. Rows run header by header, the umbrella's macros first and the C ABI last. In the cells a bare number (3.3, 6.1) is a section of `PLAN.md`, Dn is its decision, CF and C1 to C9 its milestones' rows, and CAT-... a contract page's requirement; the plan's own reasons are quoted as the CF row words them. Where the plan gives a construct no name the cell describes it. One public signature exposes a `detail` type: `basic_iir_cascade`'s public base class and its public alias `base` are spelled `detail::iir_cascade_chain<...>::type` (which is `tap::dsp::chain<...>`); elsewhere public signatures expose only DspTap's public types (`tap::dsp::exact_ratio`, `iir_state<S>`, the half-band and Chebyshev II design and table types). The tables have 647 rows: `async` 152, `bridge` 104, `rational` 391; 53 of them are not settled by the plan, listed again under ten questions at the end.

## `async` (`tap::sr::async`)

| SampleRateTap | Kind | Lands in catenae as | Or dropped because |
|---|---|---|---|
| `TAP_SR_VERSION_MAJOR` | macro | — | SampleRateTap's family version (its D13); catenae is not of that family (D14) and depends on neither library (D3, D15) |
| `TAP_SR_VERSION_MINOR` | macro | — | SampleRateTap's family version (its D13); catenae is not of that family (D14) and depends on neither library (D3, D15) |
| `TAP_SR_VERSION_PATCH` | macro | — | SampleRateTap's family version (its D13); catenae is not of that family (D14) and depends on neither library (D3, D15) |
| `TAP_SR_DSPTAP_MINIMUM` | macro | — | A floor on a DspTap dependency: DspTap's code is carried by copy with its commit recorded in `NOTICE.md`, and nothing of it is linked (D3) |
| `TAP_SR_ASYNC_NOINLINE` | macro | Inlining control in the one compiler-extension header, one recorded deviation (8; 6.1) | The engine's own spelling |
| `config` | type | The `async` junction's configuration (D19, D34; 6.1), at C8 | — |
| `config::sample_rate_hz` | data member | — | Not settled by the plan (Open: a rate in Hz) |
| `config::target_latency_frames` | data member | The junction's configured target, the fill it starts from (12: `async` starts from its configured targets) | Whether a template argument, as `handoff`'s `Target` is, or a configured number: not settled by the plan (Open: the junction's target) |
| `config::fifo_frames` | data member | `capacity_frames`, a template argument and a power of two, asserted (6.1) | A ring sized at run time (CF); its automatic size (0) |
| `config::filter` | data member | The bank's table design, held by view (3.3, D37, D54) | Run-time profile objects (3.3, CF) |
| `config::servo` | data member | The servo's parameters, part of the junction's configuration (D34) | — |
| `config::for_sample_rate(double)` | member function | — | Not settled by the plan (Open: a rate in Hz) |
| `converter_state` | type | The state field of `async`'s report type (6.1) | — |
| `converter_state::filling` | enumerator | A value of the report's state field (6.1) | — |
| `converter_state::acquiring` | enumerator | A value of the report's state field (6.1) | — |
| `converter_state::locked` | enumerator | A value of the report's state field (6.1) | — |
| `converter_status` | type | `async`'s own report type, returned by `report(const state&) const` on any thread (6.1) | Fields of `double` and 64 bits: each field is one atomic 32-bit word, with no snapshot across fields (6.1) |
| `converter_status::state` | data member | The report's state field (6.1) | — |
| `converter_status::ratio_estimate` | data member | The report's rate estimate (6.1) | — |
| `converter_status::ppm` | data member | The report's rate estimate (6.1) | — |
| `converter_status::fifo_fill_frames` | data member | The report's fill, one word the consumer publishes, `async`'s form (6.1) | — |
| `converter_status::underruns` | data member | The report's underrun count (6.1) | — |
| `converter_status::overruns` | data member | The report's overrun count (6.1) | — |
| `converter_status::resyncs` | data member | — | Not settled by the plan (Open: the high-water resync) |
| `converter_status::effective_target_latency_frames` | data member | The nominal of `latency(const state&)`, which takes the state because a junction that follows a clock moves its target as it runs (6.1) | — |
| `converter_base<S>` | type | Nothing as a type: 7.1 shares code across channel counts by keeping storage in the template and arithmetic in a kernel over spans, and the servo is compiled once (7.1) | A class that holds the design, ring, servo and `std::atomic` telemetry: configuration apart from state (D34), and the shared words are plain 32-bit words under atomic operations (D19) |
| `converter_base(const converter_base&) = delete` | member function | — | A configuration is trivially copyable (D54); a state is plain storage that is not copied while either side runs, an assumption of use (6.1, 8) |
| `converter_base::operator=(const converter_base&) = delete` | member function | — | A configuration is trivially copyable (D54); a state is plain storage that is not copied while either side runs, an assumption of use (6.1, 8) |
| `converter_base::status()` | member function | `report(const state&) const`, any thread (6.1) | — |
| `converter_base::designed_latency_seconds()` | member function | `latency(const state&) const`: `junction_latency{nominal, range}` in source frames, any thread (6.1) | Seconds: the contract states it in source frames (6.1) |
| `converter_base::filter_bank()` | member function | An accessor of the configuration's design, as a stage's `design()` (3.2; 6.1: a junction is a configuration and a state) | — |
| `basic_converter<S, Ch>` | type | The `async` junction: a configuration and a plain-storage state at its full channel count, `clocking` `clocks::independent` (D16, D19, D42; 6.1), at C8 | Members built and allocated by its constructor (D22, D34) |
| `basic_converter::channels()` | member function | The `Ch` of `sink_port` and `source_port`, `port<S, Ch>` (3.1, 6.1) | — |
| `basic_converter::validate(const config&)` | member function | The bank design's `make()`, returning `expected<T, status>` (D37, D21); the sizes and the capacity asserted on the type (6.1) | A `const char*` reason: failure is a `status` (D21) |
| `basic_converter(const config&)` | member function | A configuration built from a design, which cannot fail (D37) | Allocation, and rejection through `tap::dsp::raise` (D22, D21; 12) |
| `basic_converter::push(const S*, std::size_t)` | member function | `push(state&, in) const`: one thread, wait-free, the frames accepted, `[[nodiscard]]` (6.1, 3.4) | — |
| `basic_converter::pull(S*, std::size_t)` | member function | `pull(state&, out) const`: the other thread, always fills, returns the frames that were real (6.1); driven by `fed_by`, one pull per request (D52) | — |
| `basic_converter::reset_from_consumer()` | member function | `restart(state&) const`, the consumer's thread while the producer runs (6.1, D51) | — |
| `converter<Ch>` | alias | The `async` junction's unsuffixed `float` alias (D24) | — |
| `converter_q15<Ch>` | alias | Its `_q15` alias, over `port<q15, Ch>` (D24, D29) | — |
| `converter_q31<Ch>` | alias | Its `_q31` alias, over `port<q31, Ch>` (D24, D29) | — |
| `servo_config` | type | The servo's parameters, in the junction's configuration (D34); the servo is compiled once (7.1) and rewritten for the `float` image at C8 (12, D36) | Its `double`s (12, D36) |
| `servo_config::acquire_bandwidth_hz` | data member | A servo parameter of the junction's configuration (D34) | Its unit: not settled by the plan (Open: a rate in Hz) |
| `servo_config::track_bandwidth_hz` | data member | A servo parameter of the junction's configuration (D34) | Its unit: not settled by the plan (Open: a rate in Hz) |
| `servo_config::quiet_bandwidth_hz` | data member | A servo parameter of the junction's configuration (D34) | Its unit: not settled by the plan (Open: a rate in Hz) |
| `servo_config::damping` | data member | A servo parameter of the junction's configuration (D34) | — |
| `servo_config::acquire_smoother_hz` | data member | A servo parameter of the junction's configuration (D34) | Its unit: not settled by the plan (Open: a rate in Hz) |
| `servo_config::track_smoother_hz` | data member | A servo parameter of the junction's configuration (D34) | Its unit: not settled by the plan (Open: a rate in Hz) |
| `servo_config::quiet_smoother_hz` | data member | A servo parameter of the junction's configuration (D34) | Its unit: not settled by the plan (Open: a rate in Hz) |
| `servo_config::lock_threshold_frames` | data member | A servo parameter of the junction's configuration (D34) | — |
| `servo_config::lock_hold_seconds` | data member | A servo parameter of the junction's configuration (D34) | Its unit: not settled by the plan (Open: a rate in Hz) |
| `servo_config::quiet_hold_seconds` | data member | A servo parameter of the junction's configuration (D34) | Its unit: not settled by the plan (Open: a rate in Hz) |
| `servo_config::unlock_threshold_frames` | data member | A servo parameter of the junction's configuration (D34) | — |
| `servo_config::max_deviation_ppm` | data member | `tolerance_ppm()`, the offset and drift an independent-clock junction absorbs (6.1; CAT-JUNCTION-003) | — |
| `servo_config::scaled_to(double)` | member function | — | Not settled by the plan (Open: a rate in Hz) |
| `pi_servo` | type | The asynchronous converter's servo, compiled once in the static library (7.1), rewritten for the `float` image with its own numbers at C8 (12, D36); its memory part of the junction's state (D19) | A constructor and default member initialisers: a junction's state is plain storage (D19; CAT-JUNCTION-002) |
| `pi_servo::lock_stage` | type | The servo's stage inside the junction, reported through the report's state field (6.1) | — |
| `pi_servo::lock_stage::acquire` | enumerator | Reported as the state field's acquiring (6.1) | — |
| `pi_servo::lock_stage::track` | enumerator | Reported as the state field's locked (6.1) | — |
| `pi_servo::lock_stage::quiet` | enumerator | Reported as the state field's locked (6.1) | — |
| `pi_servo(const servo_config&, double, double)` | member function | The servo's part of the junction's state, brought to rest by the junction's `reset` and `restart` (6.1) | A constructor: a junction's state has none (D19) |
| `pi_servo::reset(bool)` | member function | Inside the junction's `restart` (the estimate forgotten) and its refill after an underrun (the estimate kept) (6.1) | Not settled by the plan (Open: the servo's rest) |
| `pi_servo::seed(double)` | member function | Inside the junction's `pull`, where the fill jumps for a known reason (6.1) | — |
| `pi_servo::set_target(double)` | member function | The moving target behind `latency(const state&)` (6.1) | Not settled by the plan (Open: the junction's target) |
| `pi_servo::update(double, double, double)` | member function | Inside the junction's `pull`, once per pull (D52, 6.1) | — |
| `pi_servo::stage()` | member function | The report's state field (6.1) | — |
| `pi_servo::locked()` | member function | The report's state field (6.1) | — |
| `pi_servo::eps_hat()` | member function | The report's rate estimate (6.1) | — |
| `pi_servo::smoothed_occupancy()` | member function | The report's fill (6.1) | — |
| `pi_servo::error()` | member function | Inside the servo (7.1) | — |
| `TAP_SR_ASYNC_CP_MIN_CHANNELS` | macro | — | Not settled by the plan (Open: the channel-parallel layout) |
| `filter_spec` | type | The spec of the bank's table design: `make(spec, table, workspace)` into caller storage, or a generated ROM table with a bit pin (3.3, D37) | — |
| `filter_spec::num_phases` | data member | A size of the bank's design type, a template argument (D37) | A run-time size, rounded up to a power of two at construction |
| `filter_spec::taps_per_phase` | data member | A size of the bank's design type, a template argument (D37) | A run-time size |
| `filter_spec::passband_hz` | data member | The design's spec (3.3) | Its unit: not settled by the plan (Open: a rate in Hz) |
| `filter_spec::stopband_hz` | data member | The design's spec (3.3) | Its unit: not settled by the plan (Open: a rate in Hz) |
| `filter_spec::stopband_atten_db` | data member | The design's spec (3.3) | — |
| `filter_spec::image_zeros` | data member | The design's spec (3.3) | — |
| `filter_spec::fast()`, `balanced()`, `transparent()`, `program()` | member function | A generated ROM table design each, one per profile (3.3) | Run-time profile objects (3.3, CF) |
| `filter_spec::scaled_to(double)` | member function | — | Not settled by the plan (Open: a rate in Hz) |
| `polyphase_filter_bank<S>` | type | The bank as a table design: a class that only `make()` or a checked `from_table()` produces (D37), in ROM or in caller storage (3.3) | A `std::vector` table and an allocating, throwing constructor (D22, D21; 12) |
| `polyphase_filter_bank::coeff` | alias | The kernel's coefficient type the design holds (D37) | — |
| `polyphase_filter_bank(const filter_spec&, double)` | member function | `make(spec, table, workspace)`, returning `expected<T, status>` (3.3, D37) | Allocation, and rejection through `tap::dsp::raise` (D22, D21; 12) |
| `polyphase_filter_bank::phase(std::size_t)` | member function | An accessor of the design's table (D37; 9.2's `coefficients()`) | — |
| `polyphase_filter_bank::num_phases()` | member function | A constant of the Design type: a design that fixes a size is a type (D37) | — |
| `polyphase_filter_bank::taps()` | member function | A constant of the Design type: a design that fixes a size is a type (D37) | — |
| `polyphase_filter_bank::group_delay_samples()` | member function | A latency the design stores as a number (D37), part of the junction's `latency(const state&)` (6.1) | — |
| `interpolate(bank, hist, double)` | function | `async`'s phase arithmetic over a header-template kernel (7.1), new numeric design at C8 (12) | — |
| `blend_row(bank, row, double)` | function | — | It calls `bank.numPhases()`, which `polyphase_filter_bank` does not declare, so it cannot be instantiated, and nothing calls it |
| `blend_row_phase(bank, row, std::uint64_t)` | function | `async`'s phase arithmetic over a header-template kernel (7.1), new numeric design at C8 (12) | — |
| `interpolate_phase(bank, hist, std::uint64_t)` | function | `async`'s phase arithmetic over a header-template kernel (7.1), new numeric design at C8 (12) | — |
| `dot_row` (`using tap::dsp::dot_row`) | function | The FIR dot kernels carried from DspTap's `fir_kernels.h` (12), header templates (7.1), with the `float` accumulator's summation order (D36, 12) | The re-export into the engine's namespace |
| `dot_rows_frame_major` (`using`) | function | The FIR dot kernels carried from DspTap's `fir_kernels.h` (12), header templates (7.1) | The re-export into the engine's namespace |
| `dot_tile_frame_major` (`using`) | function | The FIR dot kernels carried from DspTap's `fir_kernels.h` (12), header templates (7.1) | The re-export into the engine's namespace |
| `fractional_resampler<S, Ch>` | type | The junction's fractional-delay datapath: its histories and Q0.64 phase in the junction's plain-storage state, sized by template arguments (D19, D22); its phase arithmetic new numeric design at C8 (12) | `std::vector` members and a constructor (D22, D19) |
| `fractional_resampler::k_channel_parallel` | constant | — | Not settled by the plan (Open: the channel-parallel layout) |
| `fractional_resampler::k_frame_major` | constant | — | Not settled by the plan (Open: the channel-parallel layout) |
| `fractional_resampler::channels()` | member function | The `Ch` of the junction's ports (3.1, 6.1) | — |
| `fractional_resampler(const polyphase_filter_bank<S>&, std::size_t)` | member function | Part of the junction's state, sized by template arguments (D22; 12, allocation) | Allocation, and rejection through `tap::dsp::raise` (D22, D21) |
| `fractional_resampler::reset()` | member function | Inside the junction's `reset` and `restart` (6.1) | — |
| `fractional_resampler::mu()` | member function | Inside the junction's `pull`, read by the servo once per pull (D52) | — |
| `fractional_resampler::primed()` | member function | A word of the junction's state (D19) | — |
| `fractional_resampler::buffered_frames()` | member function | Part of the fill the report publishes and the servo reads (6.1) | — |
| `fractional_resampler::prime(PopFn&&)` | member function | Inside the junction's `pull` (6.1) | — |
| `fractional_resampler::process(S*, std::size_t, double, PopFn&&)` | member function | Inside the junction's `pull` (6.1) | — |
| `sample_traits<T>` | type | catenae's `sample_traits`, carried from DspTap (12), found from a `fixed<>` tag (D29; 12, written new) | — |
| `sample_traits<float>` | type | The `float` profile's traits, accumulating in `float` (D36) | Its `double` accumulator (D36) |
| `sample_traits<std::int16_t>` | type | The traits of `q15`, `fixed<std::int16_t, 15>` (3.1, D29) | Keyed on a raw `int16_t`, which is not a sample format (3.1) |
| `sample_traits<std::int32_t>` | type | The traits of `q31`, and of Q4.27 through the same kernel (3.1, D29) | Keyed on a raw `int32_t`, which is not a sample format (3.1) |
| `sample_traits<T>::blend_factor` | alias | `async`'s phase arithmetic, new numeric design at C8 (12) | — |
| `sample_traits<T>::make_blend_factor(double)` | member function | `async`'s phase arithmetic, new numeric design at C8 (12) | — |
| `sample_traits<T>::blend_factor_from_q64(std::uint64_t)` | member function | `async`'s phase arithmetic, new numeric design at C8 (12) | — |
| `sample_traits<T>::blend(coeff, coeff, blend_factor)` | member function | `async`'s phase arithmetic, new numeric design at C8 (12) | — |
| `sample_traits<T>::coeff` (from `tap::dsp::sample_traits`) | alias | catenae's `sample_traits`, carried from DspTap (12): Q1.14 and Q1.30 coefficients for the fixed formats | — |
| `sample_traits<T>::accum` (inherited) | alias | catenae's `sample_traits`, carried from DspTap (12): a 64-bit accumulator for the fixed formats | `double` for `float` (D36) |
| `sample_traits<T>::k_coeff_scale` (inherited) | constant | catenae's `sample_traits`, carried from DspTap (12), named without `k_` (D30) | — |
| `sample_traits<T>::k_is_fixed_point` (inherited) | constant | catenae's `sample_traits`, carried from DspTap (12), named without `k_` (D30) | — |
| `sample_traits<T>::make_coeff(double)` (inherited) | member function | catenae's `sample_traits`, carried from DspTap (12) | — |
| `sample_traits<T>::mac(accum, S, coeff)` (inherited) | member function | catenae's `sample_traits`, carried from DspTap (12), its order of summation fixed before a floor is recorded (D36) | — |
| `sample_traits<T>::finalize(accum)` (inherited) | member function | catenae's `sample_traits`, carried from DspTap (12): one rounding point, a saturating output | — |
| `sample_traits<T>::silence()` (inherited) | member function | catenae's `sample_traits`, carried from DspTap (12) | — |
| `sample_traits<T>::k_sample_frac_bits` (inherited, Q15 and Q31) | constant | catenae's `sample_traits`, carried from DspTap (12), named without `k_` (D30); the format's `F` (D29) | — |
| `sample_traits<T>::k_coeff_frac_bits` (inherited, Q15 and Q31) | constant | catenae's `sample_traits`, carried from DspTap (12), named without `k_` (D30) | — |
| `sample_traits<T>::k_accum_pre_shift` (inherited, Q15 and Q31) | constant | catenae's `sample_traits`, carried from DspTap (12), named without `k_` (D30) | — |
| `sample_traits<T>::k_accum_frac_bits` (inherited, Q15 and Q31) | constant | catenae's `sample_traits`, carried from DspTap (12), named without `k_` (D30) | — |
| `sample_traits<T>::k_finalize_shift` (inherited, Q15 and Q31) | constant | catenae's `sample_traits`, carried from DspTap (12), named without `k_` (D30) | — |
| `sample_type<T>` | concept | The formats the junction's battery pins (D29, 3.1) | A C++20 concept (D2) |
| `spsc_ring<T>` | type | The ring of `handoff<S, Ch, Capacity, Target, Push>` (6.1; 12; `NOTICE.md`), and the `async` junction's | A ring sized at run time (CF); `std::vector` storage (D22); `std::atomic` indices: the shared words are plain 32-bit words (D19) |
| `spsc_ring(std::size_t)` | member function | `capacity_frames`, a template argument and a power of two, asserted (6.1) | A ring sized at run time (CF) |
| `spsc_ring(const spsc_ring&) = delete` | member function | — | A state is plain storage, not copied while either side runs (6.1, 8) |
| `spsc_ring::operator=(const spsc_ring&) = delete` | member function | — | A state is plain storage, not copied while either side runs (6.1, 8) |
| `spsc_ring::capacity()` | member function | `capacity_frames` (6.1) | — |
| `spsc_ring::write(const T*, std::size_t)` | member function | `push(state&, in) const`, whole frames checked at the boundary (6.1, 3.4) | — |
| `spsc_ring::write_available()` | member function | The fill in `report(const state&)`, any thread (6.1) | — |
| `spsc_ring::read(T*, std::size_t)` | member function | `pull(state&, out) const`: always fills, silence for what it could not supply (6.1) | — |
| `spsc_ring::read_available()` | member function | The fill in `report(const state&)`, any thread (6.1) | — |
| `spsc_ring::discard(std::size_t)` | member function | `handoff`'s discard of the oldest frames above `Target` on the pull that starts paying out (D51), and `restart` (6.1) | — |
| `TAP_SR_ASYNC_API` | macro | The export decoration of the C header the manifest expands, for the shared target `catenae_c` (7.5) and its exported-symbol pin (C6) | The engine's own spelling |
| `tap_sr_async_converter` | C type | — | An opaque handle: nothing is chosen at run time behind a handle, and storage is the caller's, its size and alignment exported per target (D45, 7.5) |
| `TAP_SR_ASYNC_FORMAT_FLOAT` | macro | — | The entry fixes its format (7.5: what an entry fixes) |
| `TAP_SR_ASYNC_FORMAT_Q15` | macro | — | The entry fixes its format (7.5: what an entry fixes) |
| `TAP_SR_ASYNC_FORMAT_Q31` | macro | — | The entry fixes its format (7.5: what an entry fixes) |
| `tap_sr_async_version` | C function | — | Not settled by the plan (Open: a C ABI version) |
| `tap_sr_async_create(double, size_t, int, unsigned)` | C function | A manifest entry per instantiation (the junction type, its format, channel count, design types and capacities), its storage the caller's (D45, 7.5) | A run-time choice of profile and channel count behind a handle (D45, D42) |
| `tap_sr_async_create_format(double, size_t, int, int, unsigned)` | C function | A manifest entry per instantiation (D45, 7.5) | A run-time choice of format, profile and channel count behind a handle (D45, D42) |
| `tap_sr_async_destroy` | C function | — | No heap, and storage is the caller's (D22, D45) |
| `tap_sr_async_format` | C function | — | The entry fixes its format (7.5: what an entry fixes) |
| `tap_sr_async_push`, `_q15`, `_q31` | C function | `push` as the entry's exported C function (D45, 7.5), a wrapper that returns a status for a wrong size (7.5), one entry per format | — |
| `tap_sr_async_pull`, `_q15`, `_q31` | C function | `pull` as the entry's exported C function (D45, 7.5), a wrapper that returns a status for a wrong size (7.5), one entry per format | — |
| `tap_sr_async_status(const tap_sr_async_converter*, double[6])` | C function | `report` as the entry's exported C function over the caller's storage (D45, 7.5) | An array of `double`s: each field is a 32-bit word (6.1) |
| `tap_sr_async_designed_latency_seconds` | C function | `latency` as the entry's exported C function over the caller's storage (D45, 7.5), in source frames (6.1) | Seconds (6.1) |
| `tap_sr_async_reset_from_consumer` | C function | `restart` as the entry's exported C function over the caller's storage (D45, 7.5) | — |

## `bridge` (`tap::sr::bridge`)

| SampleRateTap | Kind | Lands in catenae as | Or dropped because |
|---|---|---|---|
| `TAP_SR_VERSION_MAJOR` | macro | — | SampleRateTap's family version (its D13); catenae is not of that family (D14) and depends on neither library (D3, D15) |
| `TAP_SR_VERSION_MINOR` | macro | — | SampleRateTap's family version (its D13); catenae is not of that family (D14) and depends on neither library (D3, D15) |
| `TAP_SR_VERSION_PATCH` | macro | — | SampleRateTap's family version (its D13); catenae is not of that family (D14) and depends on neither library (D3, D15) |
| `TAP_SR_DSPTAP_MINIMUM` | macro | — | A floor on a DspTap dependency: DspTap's code is carried by copy with its commit recorded in `NOTICE.md`, and nothing of it is linked (D3) |
| `k_phases_up` | constant | The bridge stage's `rate_ratio`, `ratio{160, 147}` (D18, 3.2) | — |
| `k_phases_down` | constant | The bridge stage's `rate_ratio`, `ratio{147, 160}` (D18, 3.2) | — |
| `direction` | type | The `Ratio` of the bridge stage, `stage<S, Ch, Ratio, Design>` (3.3; CF: one `bridge` stage with its table design), at C7 | — |
| `direction::up_to_48k` | enumerator | `Ratio` 160/147 (3.3) | — |
| `direction::down_to_44k1` | enumerator | `Ratio` 147/160 (3.3) | — |
| `ratio_traits<D, K>` | type | The stage's `rate_ratio` (3.2) and its table design's inputs (3.3) | — |
| `ratio_traits::k_rate_scale` | constant | — | Not settled by the plan (Open: bridge's rate scale) |
| `ratio_traits::k_rate_factor` | constant | — | Not settled by the plan (Open: bridge's rate scale) |
| `ratio_traits::k_phases` | constant | `rate_ratio`'s `up` (3.2) | — |
| `ratio_traits::k_decimation` | constant | `rate_ratio`'s `down` (3.2) | — |
| `ratio_traits::k_input_rate_hz` | constant | An input of the table's generator, a host tool (3.3, C3) | As a member of the stage: a port carries no rate (3.1) |
| `ratio_traits::k_output_rate_hz` | constant | An input of the table's generator, a host tool (3.3, C3) | As a member of the stage: a port carries no rate (3.1) |
| `ratio_traits::k_stopband_edge_hz` | constant | An input of the table's generator, a host tool (3.3, C3) | As a member of the stage: a port carries no rate (3.1) |
| `profile` | type | A Design type per named profile, its tap count a template argument (3.3, D37); a custom one is `custom<Taps>`, filled by `make(spec, table, workspace)` (3.3) | Run-time profile objects (3.3, CF) |
| `profile::passband_hz` | data member | The spec of the table's generator, or of `custom<Taps>`'s `make` (3.3) | — |
| `profile::stopband_atten_db` | data member | The spec of the table's generator, or of `custom<Taps>`'s `make` (3.3) | — |
| `profile::taps_up_to_48k` | data member | The Design's tap count, a template argument (D37, 3.3) | — |
| `profile::taps_down_to_44k1` | data member | The Design's tap count, a template argument (D37, 3.3) | — |
| `profile::economy()`, `super_economy()`, `balanced()`, `transparent()` | member function | A generated ROM table design each, with its tap count and a bit pin (3.3) | Run-time profile objects (3.3, CF) |
| `profile::taps<D>()` | member function | A constant of the Design type: a design that fixes a size is a type (D37) | — |
| `validate_profile<D, K>(const profile&)` | function | `custom<Taps>`'s `make()`, returning `expected<T, status>` (D37, D21) | A `const char*` reason: failure is a `status` (D21) |
| `design_prototype<D, K>(const profile&)` | function | The Kaiser designer, compiled once (7.1), run by the table generator, a host tool (3.3, C3), and by `make(spec, table, workspace)` into caller storage (3.3) | A returned `std::vector<double>` (D22; 12) |
| `schedule_entry` | type | The compile-time schedule inside the rate-changing stage `stage<S, Ch, Ratio, Design>` (3.3), modelled on rational's `basic_stage` (12) | — |
| `schedule_entry::phase` | data member | Inside the stage's compile-time schedule (3.3; 12) | — |
| `schedule_entry::advance` | data member | Inside the stage's compile-time schedule (3.3; 12) | — |
| `make_schedule<D>()` | function | Inside the stage's compile-time schedule (3.3; 12) | — |
| `k_schedule<D>` | constant | Inside the stage's compile-time schedule (3.3; 12) | — |
| `frames_needed<D>(std::size_t, std::uint64_t)` | function | `frames_needed(const state&, k)` (3.2), the position held in the state | — |
| `basic_phase_table<S, D, K>` | type | The bridge stage's table design (CF; 3.3, D37): a generated ROM table with a bit pin per profile, or built by `make(spec, table, workspace)` into caller storage | A `std::vector` table and an allocating constructor (D22; 12) |
| `basic_phase_table::coeff` | alias | The kernel's coefficient type the design holds (D37) | — |
| `basic_phase_table::k_phases` | constant | `rate_ratio`'s `up` (3.2) | — |
| `basic_phase_table::k_rate_scale` | constant | — | Not settled by the plan (Open: bridge's rate scale) |
| `basic_phase_table::k_stored` | constant | A `constexpr` size function of the table design (3.3) | — |
| `basic_phase_table::is_mirrored(std::size_t)` | member function | Inside the design's table layout (3.3, D37) | — |
| `basic_phase_table::stored_index(std::size_t)` | member function | Inside the design's table layout (3.3, D37) | — |
| `basic_phase_table(const profile&)` | member function | `make(spec, table, workspace)`, or a generated ROM table (3.3) | Allocation and a run-time design in a constructor (D22, D37; 12) |
| `basic_phase_table::stored_row(std::size_t)` | member function | An accessor of the design's table (D37; 9.2's `coefficients()`) | — |
| `basic_phase_table::at(std::size_t, std::size_t)` | member function | An accessor of the design's table (D37; 9.2's `coefficients()`) | — |
| `basic_phase_table::taps()` | member function | A constant of the Design type: a design that fixes a size is a type (D37) | — |
| `basic_phase_table::group_delay_input_samples()` | member function | The latency the design stores (D37), the stage's `exact_latency` (D39) | In input samples: latency is in output frames (D39, 3.2) |
| `basic_phase_table::storage_bytes()` | member function | A `constexpr` size function of the table design (3.3) | — |
| `TAP_SR_BRIDGE_MIRRORED_DOT_ATTR` | macro | Inlining control in the one compiler-extension header, one recorded deviation (8; 6.1) | The engine's own spelling |
| `TAP_SR_BRIDGE_NOINLINE` | macro | Inlining control in the one compiler-extension header, one recorded deviation (8; 6.1) | The engine's own spelling |
| `converter_base<S, D, K>` | type | Nothing as a type: 7.1 shares code across channel counts by keeping storage in the template and arithmetic in a kernel over spans | A class that holds design, buffers and stream position together: configuration apart from state (D34) |
| `converter_base::coeff` | alias | The kernel's coefficient type the design holds (D37) | — |
| `converter_base::k_phases` | constant | `rate_ratio`'s `up` (3.2) | — |
| `converter_base::k_decimation` | constant | `rate_ratio`'s `down` (3.2) | — |
| `converter_base::k_rate_scale` | constant | — | Not settled by the plan (Open: bridge's rate scale) |
| `converter_base::k_taps_economy`, `k_taps_super_economy`, `k_taps_balanced`, `k_taps_transparent` | constant | Each profile's Design type carries its tap count as a compile-time constant (D37, 3.3) | The run-time dispatch over them: a design that fixes a size is a type (D37) |
| `converter_base::validate(const profile&)` | member function | `custom<Taps>`'s `make()` (D37, D21) | A `const char*` reason: failure is a `status` (D21) |
| `converter_base::frames_needed(std::uint64_t)` | member function | `frames_needed(const state&, k)` (3.2) | — |
| `converter_base::outputs_for(std::uint64_t)` | member function | `outputs_for(const state&, n)` (3.2) | — |
| `converter_base::flush_output_frames()` | member function | — | The flush is resumable: a call fills the buffer it is given and the flush is finished when `flush_remaining(state)` is 0 (D9, 4) |
| `converter_base::latency_input_frames()` | member function | `has_exact_latency` and `exact_latency`, a reduced `ratio` in output frames and a constant of the type; `latency()` with deviation 0 (D39, 3.2) | In input frames (D39) |
| `converter_base::taps()` | member function | A constant of the Design type: a design that fixes a size is a type (D37) | — |
| `converter_base::position()` | member function | A field of the stage's state, reached by `stage_state<I>(state)` (4) | — |
| `converter_base::table()` | member function | The stage's `design()` accessor (3.2) | — |
| `basic_converter<S, Ch, D, K>` | type | `stage<S, Ch, Ratio, Design>` with bridge's ratio and table design (3.3; CF), at C7 | An allocating, throwing constructor (D22, D21; 12) |
| `basic_converter::channels()` | member function | The `Ch` of the stage's ports, `port<S, Ch>` (3.1) | — |
| `basic_converter(const profile&)` | member function | A configuration built from its Design type, which cannot fail (D37, 3.3) | Run-time profile objects (3.3, CF) |
| `basic_converter::process(const S*, std::size_t, S*)` | member function | The stage's trusted `run(state&, workspace&, in, out) const` (3.2), and the checked `process` of a chain of one (3.4, 4) | — |
| `basic_converter::pull(S*, std::size_t, PopFn&&)` | member function | `fed_by`, the exact pull from a junction (6.1, D52); from a source on the same thread, `frames_needed(state, k)` then `process` (3.2, 4) | A pull through a callback: the stage contract has no `pull` (3.2) |
| `basic_converter::flush(S*)` | member function | The resumable `flush(state&, workspace&, out)` of a chain of one (4, D9; 3.4), over the stage's `tail_frames()` (3.2) | — |
| `basic_converter::reset()` | member function | `reset(state&) const noexcept`, writing its non-zero rest with one pre-advance pending, as rational's stage does (D49; 12) | — |
| `converter_to_48k<Ch>` | alias | The bridge stage's unsuffixed `float` alias at 160/147 (D24) | — |
| `converter_to_44k1<Ch>` | alias | The bridge stage's unsuffixed `float` alias at 147/160 (D24) | — |
| `converter_to_48k_q15<Ch>` | alias | Its `_q15` alias at 160/147 (D24, D29) | — |
| `converter_to_44k1_q15<Ch>` | alias | Its `_q15` alias at 147/160 (D24, D29) | — |
| `converter_to_48k_q31<Ch>` | alias | Its `_q31` alias at 160/147 (D24, D29) | — |
| `converter_to_44k1_q31<Ch>` | alias | Its `_q31` alias at 147/160 (D24, D29) | — |
| `converter_to_96k<Ch>` | alias | — | Not settled by the plan (Open: bridge's rate scale) |
| `converter_to_88k2<Ch>` | alias | — | Not settled by the plan (Open: bridge's rate scale) |
| `converter_to_96k_q15<Ch>` | alias | — | Not settled by the plan (Open: bridge's rate scale) |
| `converter_to_88k2_q15<Ch>` | alias | — | Not settled by the plan (Open: bridge's rate scale) |
| `converter_to_96k_q31<Ch>` | alias | — | Not settled by the plan (Open: bridge's rate scale) |
| `converter_to_88k2_q31<Ch>` | alias | — | Not settled by the plan (Open: bridge's rate scale) |
| `converter_to_192k<Ch>` | alias | — | Not settled by the plan (Open: bridge's rate scale) |
| `converter_to_176k4<Ch>` | alias | — | Not settled by the plan (Open: bridge's rate scale) |
| `converter_to_192k_q15<Ch>` | alias | — | Not settled by the plan (Open: bridge's rate scale) |
| `converter_to_176k4_q15<Ch>` | alias | — | Not settled by the plan (Open: bridge's rate scale) |
| `converter_to_192k_q31<Ch>` | alias | — | Not settled by the plan (Open: bridge's rate scale) |
| `converter_to_176k4_q31<Ch>` | alias | — | Not settled by the plan (Open: bridge's rate scale) |
| `TAP_SR_BRIDGE_API` | macro | The export decoration of the C header the manifest expands, for the shared target `catenae_c` (7.5) and its exported-symbol pin (C6) | The engine's own spelling |
| `tap_sr_bridge_converter` | C type | — | An opaque handle: nothing is chosen at run time behind a handle, and storage is the caller's, its size and alignment exported per target (D45, 7.5) |
| `TAP_SR_BRIDGE_FORMAT_FLOAT` | macro | — | The entry fixes its format (7.5: what an entry fixes) |
| `TAP_SR_BRIDGE_FORMAT_Q15` | macro | — | The entry fixes its format (7.5: what an entry fixes) |
| `TAP_SR_BRIDGE_FORMAT_Q31` | macro | — | The entry fixes its format (7.5: what an entry fixes) |
| `tap_sr_bridge_create(int, int, unsigned)` | C function | A manifest entry per stage type, format, channel count and Design (D45, 7.5) | A run-time choice of direction, profile and channel count behind a handle, and its fan-out of one-channel converters: run-time counts are outside the library (D45, D42) |
| `tap_sr_bridge_create_format(int, int, int, unsigned)` | C function | A manifest entry per stage type, format, channel count and Design (D45, 7.5) | A run-time choice of direction, profile, format and channel count behind a handle, and its fan-out (D45, D42) |
| `tap_sr_bridge_destroy` | C function | — | No heap, and storage is the caller's (D22, D45) |
| `tap_sr_bridge_format` | C function | — | The entry fixes its format (7.5: what an entry fixes) |
| `tap_sr_bridge_outputs_for` | C function | `outputs_for` as the entry's exported C function over the caller's storage (D45, 7.5) | — |
| `tap_sr_bridge_frames_needed` | C function | `frames_needed` as the entry's exported C function over the caller's storage (D45, 7.5) | — |
| `tap_sr_bridge_process`, `_q15`, `_q31` | C function | `process` as the entry's exported C function (D45, 7.5), a wrapper that returns a status for a wrong size (7.5), one entry per format | — |
| `tap_sr_bridge_flush`, `_q15`, `_q31` | C function | The resumable `flush` (4, D9) as the entry's exported C function (D45, 7.5), a wrapper that returns a status for a wrong size (7.5), one entry per format | — |
| `tap_sr_bridge_flush_output_frames` | C function | — | The flush is resumable: a call fills the buffer it is given and the flush is finished when `flush_remaining(state)` is 0 (D9, 4) |
| `tap_sr_bridge_reset` | C function | The entry's `init(configuration, configuration_size, state, state_size)`, which is `reset` and checks the sizes, returning a status (D45, 7.5) | — |
| `tap_sr_bridge_latency_input_frames` | C function | `exact_latency` in output frames (D39) as the entry's exported C function over the caller's storage (D45, 7.5) | In input frames (D39) |
| `tap_sr_bridge_taps` | C function | A type-level fact of the entry, its Design's tap count (7.5, D37) | A run-time query of a handle (D45) |
| `tap_sr_bridge_version` | C function | — | Not settled by the plan (Open: a C ABI version) |

## `rational` (`tap::sr::rational`)

| SampleRateTap | Kind | Lands in catenae as | Or dropped because |
|---|---|---|---|
| `TAP_SR_VERSION_MAJOR` | macro | — | SampleRateTap's family version (its D13); catenae is not of that family (D14) and depends on neither library (D3, D15) |
| `TAP_SR_VERSION_MINOR` | macro | — | SampleRateTap's family version (its D13); catenae is not of that family (D14) and depends on neither library (D3, D15) |
| `TAP_SR_VERSION_PATCH` | macro | — | SampleRateTap's family version (its D13); catenae is not of that family (D14) and depends on neither library (D3, D15) |
| `TAP_SR_DSPTAP_MINIMUM` | macro | — | A floor on a DspTap dependency: DspTap's code is carried by copy with its commit recorded in `NOTICE.md`, and nothing of it is linked (D3) |
| `TAP_SR_RATIONAL_NOINLINE` | macro | Inlining control in the one compiler-extension header, one recorded deviation (8; 6.1) | The engine's own spelling |
| `TAP_SR_RATIONAL_ALWAYS_INLINE` | macro | Inlining control in the one compiler-extension header, one recorded deviation (8; 6.1) | The engine's own spelling |
| `ratio<L, M>` | type | `catenae::ratio_t<L, M>`, the `Ratio` of `stage<S, Ch, Ratio, Design>` (3.3; CF finding F3, CAT-RATIO-004); the stage's `rate_ratio` is its `value` (3.2, D18) | Its charter's `static_assert`s: not settled by the plan (Open: the ratio as a type) |
| `ratio<L, M>::k_up` | constant | `ratio_t<L, M>::up`, `rate_ratio`'s `up` (3.2) | — |
| `ratio<L, M>::k_down` | constant | `ratio_t<L, M>::down`, `rate_ratio`'s `down` (3.2) | — |
| `is_ratio<T>` | type | — | Not settled by the plan (Open: the ratio as a type) |
| `rational_ratio` | concept | — | A C++20 concept (D2) |
| `ratio_traits<R>` | type | The stage's `rate_ratio` and its machine (3.2, 3.3) | — |
| `ratio_traits::k_up` | constant | `rate_ratio`'s `up` (3.2) | — |
| `ratio_traits::k_down` | constant | `rate_ratio`'s `down` (3.2) | — |
| `ratio_traits::k_is_up` | constant | Derived from `rate_ratio` (3.2) | — |
| `ratio_traits::k_is_interpolator` | constant | The machine inside `stage<S, Ch, Ratio, Design>` (12); a chain reads its effect as `input_group`, `max_shortfall` and `max_lead` (3.2, D35) | — |
| `ratio_traits::k_is_decimator` | constant | The machine inside `stage<S, Ch, Ratio, Design>` (12); a chain reads its effect as `input_group`, `max_shortfall` and `max_lead` (3.2, D35) | — |
| `ratio_traits::k_is_mixed` | constant | The machine inside `stage<S, Ch, Ratio, Design>` (12); a chain reads its effect as `input_group`, `max_shortfall` and `max_lead` (3.2, D35) | — |
| `ratio_traits::k_band` | constant | The band of the stage's Design (3.3) | — |
| `ratio_traits::k_composite_factor` | constant | Inside the stage's Design (3.3) | — |
| `ratio_traits::k_lower_rate_num` | constant | The divisor arithmetic of the named chains' alias templates (3.3) | — |
| `ratio_traits::k_lower_rate_den` | constant | The divisor arithmetic of the named chains' alias templates (3.3) | — |
| `ratio_traits::k_pow2_up` | constant | — | Read by SampleRateTap's tests alone; the named chains are written out as alias templates (3.3) |
| `ratio_traits::k_pow3_up` | constant | — | Read by SampleRateTap's tests alone; the named chains are written out as alias templates (3.3) |
| `ratio_traits::k_pow2_down` | constant | — | Read by SampleRateTap's tests alone; the named chains are written out as alias templates (3.3) |
| `ratio_traits::k_pow3_down` | constant | — | Read by SampleRateTap's tests alone; the named chains are written out as alias templates (3.3) |
| `ratio_traits::k_rate_ratio` | constant | — | A `double` of an exact ratio: `rate_ratio` is exact (3.2) |
| `up2` | alias | catenae's `up2` stage (9.4, C3), one pinned design per profile and design divisor (3.3); the ratio is `ratio_t<2, 1>` (CF finding F17) | — |
| `down2` | alias | catenae's `down2` stage (9.4, C3), one pinned design per profile and design divisor (3.3); the ratio is `ratio_t<1, 2>` | — |
| `up3` | alias | `ratio_t<3, 1>`, the `Ratio` of a `stage<S, Ch, Ratio, Design>` (3.3), at C7 | — |
| `down3` | alias | The `Ratio` of a `stage<S, Ch, Ratio, Design>` (3.3), at C7 | — |
| `up4` | alias | The `Ratio` of a `stage<S, Ch, Ratio, Design>` (3.3), at C7 | — |
| `down4` | alias | The `Ratio` of a `stage<S, Ch, Ratio, Design>` (3.3), at C7 | — |
| `up6` | alias | The `Ratio` of a `stage<S, Ch, Ratio, Design>` (3.3), at C7 | — |
| `down6` | alias | The `Ratio` of a `stage<S, Ch, Ratio, Design>` (3.3), at C7 | — |
| `up8` | alias | The `Ratio` of a `stage<S, Ch, Ratio, Design>` (3.3), at C7 | — |
| `down8` | alias | The `Ratio` of a `stage<S, Ch, Ratio, Design>` (3.3), at C7 | — |
| `ratio3over2` | alias | The `Ratio` of a `stage<S, Ch, Ratio, Design>` (3.3), at C7 | — |
| `ratio2over3` | alias | The `Ratio` of a `stage<S, Ch, Ratio, Design>` (3.3), at C7 | — |
| `ratio4over3` | alias | The `Ratio` of a `stage<S, Ch, Ratio, Design>` (3.3), at C7 | — |
| `ratio3over4` | alias | The `Ratio` of a `stage<S, Ch, Ratio, Design>` (3.3), at C7 | — |
| `ratio8over3` | alias | The `Ratio` of a `stage<S, Ch, Ratio, Design>` (3.3), at C7 | — |
| `ratio3over8` | alias | The `Ratio` of a `stage<S, Ch, Ratio, Design>` (3.3), at C7 | — |
| `relaxed_pins` | type | The (profile, design divisor) key of a generated Design (3.3) | A run-time table of pins, read through run-time profile objects (3.3, CF) |
| `relaxed_pins::divisor` | data member | The design divisor of a generated Design (3.3) | — |
| `relaxed_pins::taps_per_branch` | data member | The tap counts of the generated Designs at that divisor, template arguments (3.3, D37) | — |
| `k_super_economy_relaxations`, `k_economy_relaxations`, `k_balanced_relaxations`, `k_transparent_relaxations` | constant | The generated Design types, one per profile and design divisor (3.3) | Run-time profile objects (3.3, CF) |
| `profile` | type | A Design type per profile and design divisor (3.3), or `custom<Taps>` filled by `make(spec, table, workspace)` (3.3) | Run-time profile objects (3.3, CF) |
| `profile::passband_frac` | data member | The spec of the generator, or of `custom<Taps>`'s `make` (3.3) | — |
| `profile::stopband_atten_db` | data member | The spec of the generator, or of `custom<Taps>`'s `make` (3.3) | — |
| `profile::taps_per_branch` | data member | The Design's tap count, a template argument (D37) | Its 0, which asks for a run-time design search (3.3, CF) |
| `profile::relaxations` | data member | The generated Designs per divisor (3.3) | A run-time span: run-time profile objects (3.3, CF) |
| `profile::economy()`, `super_economy()`, `balanced()`, `transparent()` | member function | A generated Design each, at every divisor the named chains use (3.3) | Run-time profile objects (3.3, CF) |
| `profile::relaxed(tap::dsp::exact_ratio)` | member function | The named chains' alias templates, which compute each stage's divisor (3.3) | At a divisor without a pinned row: a stage created at an unpinned divisor (CF) |
| `profile::passband_hz(double)` | member function | — | Run-time profile objects (3.3, CF); a port carries no rate (3.1) |
| `profile::pinned_taps_per_branch(std::size_t)` | member function | The Design type's tap count (D37) | Run-time profile objects (3.3, CF) |
| `k_design_margin_db` | constant | The design rule of the table generator, a host tool (3.3, C3), and of `custom<Taps>`'s `make` (3.3); without `k_` if public (D30) | — |
| `k_design_beta_offset_db` | constant | The design rule of the table generator (3.3, C3) and of `custom<Taps>`'s `make` (3.3); without `k_` if public (D30) | — |
| `design_beta(const profile&)` | function | The design rule of the table generator (3.3, C3) and of `custom<Taps>`'s `make` (3.3) | — |
| `k_design_grid_points` | constant | The host tool's search (3.3) | A run-time design search (3.3, CF) |
| `k_design_max_taps_per_branch` | constant | The host tool's search (3.3) | A run-time design search (3.3, CF) |
| `validate(const profile&)` | function | The design's `make()`, returning `expected<T, status>` (D37, D21) | A `const char*` reason: failure is a `status` (D21) |
| `k_unmeetable_spec_reason` | constant | — | A string from a run-time design search (3.3, CF) |
| `taps_per_branch_for(std::size_t, const profile&)` | function | The pinned tap count of a Design type (D37); the search is the host tool's (3.3) | A run-time design search (3.3, CF) |
| `design_stage<R>(const profile&)` | function | The Nyquist designer carried from DspTap (12), compiled once (7.1), run by the table generator, a host tool (3.3, C3), and by `custom<Taps>`'s `make(spec, table, workspace)` (3.3) | A returned `std::vector<double>` (D22; 12) |
| `stage_taps_per_phase<R>(const profile&)` | function | A `constexpr` size function of the Design (3.3, D37) | — |
| `stage_group_delay_composite(const std::vector<double>&)` | function | The latency the Design stores (D37), the stage's `exact_latency` (D39) | — |
| `schedule_entry<R>` | type | The compile-time schedule inside `stage<S, Ch, Ratio, Design>` (3.3; 12) | — |
| `schedule_entry<R>::advance_type` | alias | Inside the stage's compile-time schedule (3.3; 12) | — |
| `schedule_entry<R>::phase` | data member | Inside the stage's compile-time schedule (3.3; 12) | — |
| `schedule_entry<R>::advance` | data member | Inside the stage's compile-time schedule (3.3; 12) | — |
| `make_schedule<R>()` | function | Inside the stage's compile-time schedule (3.3; 12) | — |
| `k_schedule<R>` | constant | Inside the stage's compile-time schedule (3.3; 12) | — |
| `schedule_frames_needed<R>(std::size_t, std::uint64_t)` | function | `frames_needed(const state&, k)` (3.2), the position held in the state | — |
| `stage_base<S, R>` | type | Nothing as a type: 7.1 shares code across channel counts by keeping storage in the template and arithmetic in a kernel over spans | A class that holds design, buffers and stream position together: configuration apart from state (D34) |
| `stage_base::sample` | alias | The sample format of the stage's ports, `port<S, Ch>` (3.1, 3.2) | — |
| `stage_base::coeff` | alias | The kernel's coefficient type the Design holds (D37) | — |
| `stage_base::traits` | alias | The stage's `rate_ratio` and its machine (3.2, 3.3) | — |
| `stage_base::k_up` | constant | `rate_ratio`'s `up` (3.2) | — |
| `stage_base::k_down` | constant | `rate_ratio`'s `down` (3.2) | — |
| `stage_base::k_band` | constant | The band of the stage's Design (3.3) | — |
| `stage_base::k_is_decimator` | constant | The machine inside the stage (12), published as `input_group`, `max_shortfall` and `max_lead` (3.2, D35) | — |
| `stage_base::k_branch_quantized` | constant | The `q15` Design and the stage's single rounding point (D37; 12), compared with SampleRateTap bit for bit in Q15 (D36) | — |
| `stage_base::k_mixed_down_gain` | constant | The `q15` Design and the stage's single rounding point (D37; 12), compared with SampleRateTap bit for bit in Q15 (D36) | — |
| `stage_base::k_row_scaled` | constant | The `q15` Design and the stage's single rounding point (D37; 12), compared with SampleRateTap bit for bit in Q15 (D36) | — |
| `stage_base::k_table_gain` | constant | The `q15` Design and the stage's single rounding point (D37; 12), compared with SampleRateTap bit for bit in Q15 (D36) | — |
| `stage_base::finalize_output(accum)` | member function | The stage's single rounding point inside `run` (12: the carried traits' one rounding point) | — |
| `stage_base::k_rows` | constant | A constant of the Design type: a design that fixes a size is a type (D37) | — |
| `stage_base::validate(const profile&)` | member function | `custom<Taps>`'s `make()` (D37, D21) | A `const char*` reason: failure is a `status` (D21); for an unpinned profile, a run-time design search (3.3, CF) |
| `stage_base::outputs_for(std::size_t)` | member function | `outputs_for(const state&, n)` (3.2) | — |
| `stage_base::frames_needed(std::size_t)` | member function | `frames_needed(const state&, k)` (3.2) | — |
| `stage_base::flush_output_frames()` | member function | — | The flush is resumable: a call fills the buffer it is given and the flush is finished when `flush_remaining(state)` is 0 (D9, 4) |
| `stage_base::reset()` | member function | `reset(state&) const noexcept`: each channel at its design's row length with one pre-advance pending (D49; 12) | — |
| `stage_base::latency_output_frames()` | member function | `has_exact_latency` and `exact_latency`, a reduced `ratio` in output frames and a constant of the type; `latency()` with deviation 0 (D39, 3.2) | — |
| `stage_base::latency_seconds(double)` | member function | — | Latency is reported in output frames (D39, 3.2), and a port carries no rate (3.1) |
| `stage_base::taps()` | member function | A constant of the Design type: a design that fixes a size is a type (D37) | — |
| `stage_base::row_length()` | member function | A constant of the Design type: a design that fixes a size is a type (D37) | — |
| `stage_base::position()` | member function | A field of the stage's state, reached by `stage_state<I>(state)` (4) | — |
| `stage_base::decimation_phase()` | member function | A field of the stage's state, reached by `stage_state<I>(state)` (4) | — |
| `stage_base::window_frames()` | member function | `tail_frames()` and `practical_tail_frames()`, with `flush_residue()` 0 for an FIR (3.2) | — |
| `stage_base::macs_per_superblock()` | member function | — | Not settled by the plan (Open: multiply counts) |
| `stage_base::macs_per_output()` | member function | — | Not settled by the plan (Open: multiply counts) |
| `stage_base::coefficient(std::size_t, std::size_t)` | member function | The Design's `coefficients()` accessor (9.2, D37) | — |
| `basic_stage<S, Ch, R>` | type | `stage<S, Ch, Ratio, Design>` (3.3), for which it is the model (12): `up2` and `down2` at C3, the rest at C7 | An allocating, throwing constructor (D22, D21; 12) |
| `basic_stage::channels()` | member function | The `Ch` of the stage's ports, `port<S, Ch>` (3.1) | — |
| `basic_stage(const profile&)` | member function | A configuration built from its Design type, which cannot fail (D37, 3.3); in a named chain, the Design at its divisor (3.3) | Run-time profile objects (3.3, CF) |
| `basic_stage::process(const S*, std::size_t, S*)` | member function | The stage's trusted `run(state&, workspace&, in, out) const` (3.2), and the checked `process` of a chain of one (3.4, 4) | — |
| `basic_stage::pull(S*, std::size_t, PopFn&&)` | member function | `fed_by`, the exact pull from a junction (6.1, D52); from a source on the same thread, `frames_needed(state, k)` then `process` (3.2, 4) | A pull through a callback: the stage contract has no `pull` (3.2) |
| `basic_stage::flush(S*)` | member function | The resumable `flush(state&, workspace&, out)` of a chain of one (4, D9; 3.4), over the stage's `tail_frames()` (3.2) | — |
| `basic_converter<S, Ch, R>` | alias | `stage<S, Ch, Ratio, Design>` (3.3) | — |
| `converter<Ch, R>` | alias | The stage's unsuffixed `float` alias (D24) | — |
| `converter_q15<Ch, R>` | alias | The stage's `_q15` alias, over `port<q15, Ch>` (D24, D29) | — |
| `converter_q31<Ch, R>` | alias | The stage's `_q31` alias, over `port<q31, Ch>` (D24, D29) | — |
| `basic_chain<S, Ch, Rs...>` | type | `chain<Stages...>`, written by the caller with `a \| b \| c` (4, D13); its stages' Designs chosen at their divisors by the named chains' alias templates (3.3) | A profile given at construction that relaxes each stage at run time: run-time profile objects (3.3, CF) |
| `basic_chain::k_stages` | constant | The length of the chain's stage list, along which `stage<I>()` counts (4) | — |
| `basic_chain::k_divisors` | constant | Computed at compile time by the named chains' alias templates (3.3) | — |
| `basic_chain::validate(const profile&)` | member function | — | A stage is built from a design and cannot fail, so a chain of them cannot (D37) |
| `basic_chain(const profile&)` | member function | `a \| b \| c` over configurations built from designs (4, D37) | Run-time profile objects (3.3, CF) |
| `basic_chain::macs_per_output_exact()` | member function | — | Not settled by the plan (Open: multiply counts) |
| `basic_chain::macs_per_output()` | member function | — | Not settled by the plan (Open: multiply counts) |
| `basic_chain::sample` (from `tap::dsp::chain`) | alias | The sample format of the chain's ports (3.1) | — |
| `basic_chain::k_channels` (from `tap::dsp::chain`) | constant | The `Ch` of the chain's ports (3.1) | — |
| `basic_chain::k_block` (from `tap::dsp::chain`) | constant | `with_block<N>(...)`, 64 frames by default (4) | — |
| `basic_chain::k_ratio` (from `tap::dsp::chain`) | constant | The chain's `rate_ratio`, the reduced product of its stages' (4; CAT-CHAIN-002) | — |
| `basic_chain::k_up` (from `tap::dsp::chain`) | constant | The chain's `rate_ratio`'s `up` (4) | — |
| `basic_chain::k_down` (from `tap::dsp::chain`) | constant | The chain's `rate_ratio`'s `down` (4) | — |
| `basic_chain::process(const sample*, std::size_t, sample*)` (from `tap::dsp::chain`) | member function | `process(state&, workspace&, in, out)`: the boundary check, then the work a block at a time (4, 3.4) | — |
| `basic_chain::outputs_for(std::size_t)` (from `tap::dsp::chain`) | member function | `outputs_for(const state&, n)`, composed (4) | — |
| `basic_chain::frames_needed(std::size_t)` (from `tap::dsp::chain`) | member function | `frames_needed(const state&, k)`, composed (4) | — |
| `basic_chain::latency_output_frames()` (from `tap::dsp::chain`) | member function | `exact_latency`, composed from the stages' (D39, 4; CAT-CHAIN-008) | — |
| `basic_chain::latency_seconds(double)` (from `tap::dsp::chain`) | member function | — | Latency is reported in output frames (D39, 3.2), and a port carries no rate (3.1) |
| `basic_chain::nominal_latency_output_frames()` (from `tap::dsp::chain`) | member function | `latency()`'s nominal, composed (3.2, 4; CAT-CHAIN-008) | — |
| `basic_chain::latency_deviation_output_frames()` (from `tap::dsp::chain`) | member function | `latency()`'s deviation, composed (3.2, 4; CAT-CHAIN-008) | — |
| `basic_chain::group_delay_output_frames(double)` (from `tap::dsp::chain`) | member function | `group_delay(f)`, a design-time free function (3.3) | As a member: `group_delay(f)` is a design-time free function, not a stage member (3.3) |
| `basic_chain::window_frames()` (from `tap::dsp::chain`) | member function | `tail_frames()`, composed backward (4; CAT-CHAIN-009) | — |
| `basic_chain::flush(sample*)` (from `tap::dsp::chain`) | member function | `flush(state&, workspace&, out)`, resumable, finished when `flush_remaining(state)` is 0 (4, D9) | A block of zeros the chain allocates: its zeros are written into the workspace (4) |
| `basic_chain::flush_output_frames()` (from `tap::dsp::chain`) | member function | — | The flush is resumable: a call fills the buffer it is given and the flush is finished when `flush_remaining(state)` is 0 (D9, 4) |
| `basic_chain::reset()` (from `tap::dsp::chain`) | member function | `reset(state&)`: each stage on its own part of the state, then `ready` set (4, D49) | — |
| `basic_chain::channels()` (from `tap::dsp::chain`) | member function | The `Ch` of the chain's ports (3.1) | — |
| `basic_chain::stage<I>()` (from `tap::dsp::chain`) | member function | `stage<I>()`, a stage's configuration by position (4) | — |
| `chain<Ch, Rs...>` | alias | `chain<Stages...>` of `float` stages, written with `\|` (4, D13, D24) | — |
| `chain_q15<Ch, Rs...>` | alias | `chain<Stages...>` of `_q15` stages (4, D24, D29) | — |
| `chain_q31<Ch, Rs...>` | alias | `chain<Stages...>` of `_q31` stages (4, D24, D29) | — |
| `up2_ratio3over2<S, Ch>` | alias | An alias template of `chain<Stages...>` that computes each stage's divisor (3.3; 4), at C7 | — |
| `up2_up2<S, Ch>` | alias | An alias template of `chain<Stages...>` that computes each stage's divisor (3.3; 4), at C7 | — |
| `up2_up3<S, Ch>` | alias | An alias template of `chain<Stages...>` that computes each stage's divisor (3.3; 4), at C7 | — |
| `up2_up4<S, Ch>` | alias | An alias template of `chain<Stages...>` that computes each stage's divisor (3.3; 4), at C7 | — |
| `up3_up4<S, Ch>` | alias | An alias template of `chain<Stages...>` that computes each stage's divisor (3.3; 4), at C7 | — |
| `up2_up6<S, Ch>` | alias | An alias template of `chain<Stages...>` that computes each stage's divisor (3.3; 4), at C7 | — |
| `up2_up4_up2<S, Ch>` | alias | An alias template of `chain<Stages...>` that computes each stage's divisor (3.3; 4), at C7 | — |
| `up2_up8<S, Ch>` | alias | An alias template of `chain<Stages...>` that computes each stage's divisor (3.3; 4), at C7 | — |
| `up2_up3_up4<S, Ch>` | alias | An alias template of `chain<Stages...>` that computes each stage's divisor (3.3; 4), at C7 | — |
| `up2_up4_up4<S, Ch>` | alias | An alias template of `chain<Stages...>` that computes each stage's divisor (3.3; 4), at C7 | — |
| `up3_up4_up4<S, Ch>` | alias | An alias template of `chain<Stages...>` that computes each stage's divisor (3.3; 4), at C7 | — |
| `up2_up6_up4<S, Ch>` | alias | An alias template of `chain<Stages...>` that computes each stage's divisor (3.3; 4), at C7 | — |
| `up2_ratio4over3<S, Ch>` | alias | An alias template of `chain<Stages...>` that computes each stage's divisor (3.3; 4), at C7 | — |
| `ratio3over4_down2<S, Ch>` | alias | An alias template of `chain<Stages...>` that computes each stage's divisor (3.3; 4), at C7 | — |
| `down2_down2<S, Ch>` | alias | An alias template of `chain<Stages...>` that computes each stage's divisor (3.3; 4), at C7 | — |
| `down3_down2<S, Ch>` | alias | An alias template of `chain<Stages...>` that computes each stage's divisor (3.3; 4), at C7 | — |
| `down4_down2<S, Ch>` | alias | An alias template of `chain<Stages...>` that computes each stage's divisor (3.3; 4), at C7 | — |
| `down4_down3<S, Ch>` | alias | An alias template of `chain<Stages...>` that computes each stage's divisor (3.3; 4), at C7 | — |
| `down6_down2<S, Ch>` | alias | An alias template of `chain<Stages...>` that computes each stage's divisor (3.3; 4), at C7 | — |
| `down2_down4_down2<S, Ch>` | alias | An alias template of `chain<Stages...>` that computes each stage's divisor (3.3; 4), at C7 | — |
| `down8_down2<S, Ch>` | alias | An alias template of `chain<Stages...>` that computes each stage's divisor (3.3; 4), at C7 | — |
| `down4_down3_down2<S, Ch>` | alias | An alias template of `chain<Stages...>` that computes each stage's divisor (3.3; 4), at C7 | — |
| `down4_down4_down2<S, Ch>` | alias | An alias template of `chain<Stages...>` that computes each stage's divisor (3.3; 4), at C7 | — |
| `down4_down4_down3<S, Ch>` | alias | An alias template of `chain<Stages...>` that computes each stage's divisor (3.3; 4), at C7 | — |
| `down4_down6_down2<S, Ch>` | alias | An alias template of `chain<Stages...>` that computes each stage's divisor (3.3; 4), at C7 | — |
| `down_by4<S, Ch>` | alias | An alias template naming the same chain as its matrix name (3.3), at C7 | — |
| `down_by8<S, Ch>` | alias | An alias template naming the same chain as its matrix name (3.3), at C7 | — |
| `down_by16<S, Ch>` | alias | An alias template naming the same chain as its matrix name (3.3), at C7 | — |
| `down_by16_transparent<S, Ch>` | alias | An alias template naming the same chain as its matrix name (3.3), at C7 | — |
| `up_by4<S, Ch>` | alias | An alias template naming the same chain as its matrix name (3.3), at C7 | — |
| `up_by8<S, Ch>` | alias | An alias template naming the same chain as its matrix name (3.3), at C7 | — |
| `up_by16<S, Ch>` | alias | An alias template naming the same chain as its matrix name (3.3), at C7 | — |
| `up_by16_transparent<S, Ch>` | alias | An alias template naming the same chain as its matrix name (3.3), at C7 | — |
| `iir_phase` | type | The variant of the recursive stage's design type (D37, 3.3) | — |
| `iir_phase::nonlinear` | enumerator | The variant of the recursive stage's design type (D37, 3.3) | — |
| `iir_phase::near_linear` | enumerator | The variant of the recursive stage's design type (D37, 3.3) | — |
| `k_iir_near_linear_profile_reason` | constant | — | A near-linear design exists only as a generated table per profile and divisor (3.3; 12, compile-time designs), so there is none without a table to reject |
| `k_iir_unmeetable_spec_reason` | constant | A `status` from the nonlinear design's `make()` (D21, D37) | A string |
| `iir_stage_base<S, R, P>` | type | Nothing as a type: 7.1 shares code across channel counts by keeping storage in the template and arithmetic in a kernel over spans | A class that holds design, buffers and stream position together: configuration apart from state (D34) |
| `iir_stage_base::sample` | alias | The sample format of the stage's ports, `port<S, Ch>` (3.1, 3.2) | — |
| `iir_stage_base::traits` | alias | The stage's `rate_ratio` and its machine (3.2, 3.3) | — |
| `iir_stage_base::state` (`tap::dsp::iir_state<S>`) | alias | Part of the stage's `state`, a plain aggregate (3.2, D34) | — |
| `iir_stage_base::k_up` | constant | `rate_ratio`'s `up` (3.2) | — |
| `iir_stage_base::k_down` | constant | `rate_ratio`'s `down` (3.2) | — |
| `iir_stage_base::k_is_decimator` | constant | The machine inside the stage, published as `input_group`, `max_shortfall` and `max_lead` (3.2, D35) | — |
| `iir_stage_base::k_factor` | constant | `rate_ratio` (3.2) | — |
| `iir_stage_base::k_phase` | constant | The variant of the recursive stage's design type (D37, 3.3) | — |
| `iir_stage_base::k_is_near_linear` | constant | The variant of the recursive stage's design type (D37, 3.3) | — |
| `iir_stage_base::table_type` | alias | The design's table in the kernel's format, over the carried `iir_traits` (12; D37) | — |
| `iir_stage_base::validate(const profile&)` | member function | The nonlinear design's `make()` (D37, D21) | A `const char*` reason: failure is a `status` (D21) |
| `iir_stage_base::outputs_for(std::size_t)` | member function | `outputs_for(const state&, n)` (3.2) | — |
| `iir_stage_base::frames_needed(std::size_t)` | member function | `frames_needed(const state&, k)` (3.2) | — |
| `iir_stage_base::window_frames()` | member function | `tail_frames()` with `flush_residue()` its stated bound, and `practical_tail_frames()` (3.2); the host's flush-to-zero an audit check and an assumption of use (3.4, 8) | — |
| `iir_stage_base::flush_output_frames()` | member function | — | The flush is resumable: a call fills the buffer it is given and the flush is finished when `flush_remaining(state)` is 0 (D9, 4) |
| `iir_stage_base::reset()` | member function | `rests_at_zero = true`: its whole rest state is zero (D49) | — |
| `iir_stage_base::nominal_latency_output_frames()` | member function | `latency()`'s nominal; `has_exact_latency` false (D39, 3.2) | — |
| `iir_stage_base::latency_deviation_output_frames()` | member function | `latency()`'s deviation (D39, 3.2) | — |
| `iir_stage_base::group_delay_output_frames(double)` | member function | `group_delay(f)`, a design-time free function (3.3) | As a member: `group_delay(f)` is a design-time free function, not a stage member (3.3) |
| `iir_stage_base::delay_path_output_frames()` | member function | — | Not settled by the plan (Open: a near-linear delay path) |
| `iir_stage_base::macs_per_output()` | member function | — | Not settled by the plan (Open: multiply counts) |
| `iir_stage_base::multiplies()` | member function | — | Not settled by the plan (Open: multiply counts) |
| `iir_stage_base::table()` | member function | The stage's `design()` accessor (3.2) | — |
| `iir_stage_base::passband()` | member function | An accessor of the design (D37) | — |
| `iir_stage_base::stopband_db()` | member function | An accessor of the design (D37) | — |
| `iir_stage_base::tabulated()` | member function | — | A design comes from `make()` or the checked `from_table()` (D37); which one is known where it is made |
| `iir_stage_base::nonlinear_design()` | member function | The stage's `design()` accessor (3.2) | — |
| `iir_stage_base::near_linear_design()` | member function | The stage's `design()` accessor (3.2) | — |
| `iir_stage_base::states()` | member function | `stage_state<I>(state)` (4); the state is the caller's plain storage (D34) | — |
| `iir_stage_base::pole_radius()` | member function | A number the design stores for its tail (D37; 3.2) | — |
| `iir_stage_base::decimation_phase()` | member function | A field of the stage's state, reached by `stage_state<I>(state)` (4) | — |
| `iir_stage_base::banked_frames()` | member function | A field of the stage's state, reached by `stage_state<I>(state)` (4) | — |
| `basic_iir_stage<S, Ch, R, P>` | type | A recursive rate-changing stage by 2 with a small design held by value (3.3), its latency nominal (D39), at C7 | An allocating constructor that may design at run time (D22, D37) |
| `basic_iir_stage::channels()` | member function | The `Ch` of the stage's ports, `port<S, Ch>` (3.1) | — |
| `basic_iir_stage(const profile&)` | member function | A configuration built from its design, which cannot fail (D37) | Run-time profile objects (3.3, CF) |
| `basic_iir_stage::process(const S*, std::size_t, S*)` | member function | The stage's trusted `run(state&, workspace&, in, out) const` (3.2), and the checked `process` of a chain of one (3.4, 4) | — |
| `basic_iir_stage::pull(S*, std::size_t, PopFn&&)` | member function | `fed_by`, the exact pull from a junction (6.1, D52); from a source on the same thread, `frames_needed(state, k)` then `process` (3.2, 4) | A pull through a callback: the stage contract has no `pull` (3.2) |
| `basic_iir_stage::flush(S*)` | member function | The resumable `flush(state&, workspace&, out)` of a chain of one (4, D9; 3.4), over the stage's `tail_frames()` (3.2) | — |
| `iir_converter<Ch, R>` | alias | The recursive stage's unsuffixed `float` alias, nonlinear variant (D24) | — |
| `iir_converter_q15<Ch, R>` | alias | Its `_q15` alias, at C6: fixed point in a recursive stage is new design (D47) | — |
| `iir_converter_q31<Ch, R>` | alias | Its `_q31` alias, at C6 (D47) | — |
| `iir_converter_near_linear<Ch, R>` | alias | The near-linear variant's unsuffixed `float` alias (D24) | — |
| `iir_converter_near_linear_q15<Ch, R>` | alias | Its `_q15` alias, at C6 (D47) | — |
| `iir_converter_near_linear_q31<Ch, R>` | alias | Its `_q31` alias, at C6 (D47) | — |
| `basic_iir_cascade<S, Ch, P, D, Up>` | type | A named chain of recursive stages by 2: an alias template of `chain<Stages...>` that computes each stage's divisor (3.3; 4) | A profile given at construction: run-time profile objects (3.3, CF) |
| `basic_iir_cascade::stage_type` | alias | The stage type in the chain's list (4) | — |
| `basic_iir_cascade::base` (`detail::iir_cascade_chain<...>::type`) | alias | `chain<Stages...>` (4) | — |
| `basic_iir_cascade::k_stages` | constant | The length of the chain's stage list (4) | — |
| `basic_iir_cascade::k_factor` | constant | The chain's `rate_ratio`, the reduced product of its stages' (4; CAT-CHAIN-002) | — |
| `basic_iir_cascade::k_divisors` | constant | Computed by the named chain's alias template (3.3) | — |
| `basic_iir_cascade::validate(const profile&)` | member function | — | A stage is built from a design and cannot fail, so a chain of them cannot (D37) |
| `basic_iir_cascade(const profile&)` | member function | `a \| b \| c` over configurations built from designs (4, D37) | Run-time profile objects (3.3, CF) |
| `basic_iir_cascade::macs_per_output()` | member function | — | Not settled by the plan (Open: multiply counts) |
| `basic_iir_cascade::latency_output_frames() = delete` | member function | The chain's `has_exact_latency`, false, composed (D39; CAT-CHAIN-008) | — |
| `basic_iir_cascade::latency_seconds(double) = delete` | member function | The chain's `has_exact_latency`, false (D39) | Latency is reported in output frames (D39, 3.2), and a port carries no rate (3.1) |
| `basic_iir_cascade::delay_path_output_frames()` | member function | — | Not settled by the plan (Open: a near-linear delay path) |
| `basic_iir_cascade::window_frames()` | member function | `tail_frames()`, composed backward (4; CAT-CHAIN-009) | — |
| `basic_iir_cascade::sample` (from `tap::dsp::chain`) | alias | The sample format of the chain's ports (3.1) | — |
| `basic_iir_cascade::k_channels` (from `tap::dsp::chain`) | constant | The `Ch` of the chain's ports (3.1) | — |
| `basic_iir_cascade::k_block` (from `tap::dsp::chain`) | constant | `with_block<N>(...)`, 64 frames by default (4) | — |
| `basic_iir_cascade::k_ratio` (from `tap::dsp::chain`) | constant | The chain's `rate_ratio`, the reduced product of its stages' (4; CAT-CHAIN-002) | — |
| `basic_iir_cascade::k_up` (from `tap::dsp::chain`) | constant | The chain's `rate_ratio`'s `up` (4) | — |
| `basic_iir_cascade::k_down` (from `tap::dsp::chain`) | constant | The chain's `rate_ratio`'s `down` (4) | — |
| `basic_iir_cascade::process(const sample*, std::size_t, sample*)` (from `tap::dsp::chain`) | member function | `process(state&, workspace&, in, out)`: the boundary check, then the work a block at a time (4, 3.4) | — |
| `basic_iir_cascade::outputs_for(std::size_t)` (from `tap::dsp::chain`) | member function | `outputs_for(const state&, n)`, composed (4) | — |
| `basic_iir_cascade::frames_needed(std::size_t)` (from `tap::dsp::chain`) | member function | `frames_needed(const state&, k)`, composed (4) | — |
| `basic_iir_cascade::nominal_latency_output_frames()` (from `tap::dsp::chain`) | member function | `latency()`'s nominal, composed (3.2, 4; CAT-CHAIN-008) | — |
| `basic_iir_cascade::latency_deviation_output_frames()` (from `tap::dsp::chain`) | member function | `latency()`'s deviation, composed (3.2, 4; CAT-CHAIN-008) | — |
| `basic_iir_cascade::group_delay_output_frames(double)` (from `tap::dsp::chain`) | member function | `group_delay(f)`, a design-time free function (3.3) | As a member: `group_delay(f)` is a design-time free function, not a stage member (3.3) |
| `basic_iir_cascade::flush(sample*)` (from `tap::dsp::chain`) | member function | `flush(state&, workspace&, out)`, resumable, finished when `flush_remaining(state)` is 0 (4, D9) | A block of zeros the chain allocates: its zeros are written into the workspace (4) |
| `basic_iir_cascade::flush_output_frames()` (from `tap::dsp::chain`) | member function | — | The flush is resumable: a call fills the buffer it is given and the flush is finished when `flush_remaining(state)` is 0 (D9, 4) |
| `basic_iir_cascade::reset()` (from `tap::dsp::chain`) | member function | `reset(state&)`: each stage on its own part of the state, then `ready` set (4, D49) | — |
| `basic_iir_cascade::channels()` (from `tap::dsp::chain`) | member function | The `Ch` of the chain's ports (3.1) | — |
| `basic_iir_cascade::stage<I>()` (from `tap::dsp::chain`) | member function | `stage<I>()`, a stage's configuration by position (4) | — |
| `iir_down_by<S, Ch, P, D>` | alias | A named chain: an alias template of `chain<Stages...>` that computes each stage's divisor (3.3), at C7 | — |
| `iir_up_by<S, Ch, P, D>` | alias | A named chain: an alias template of `chain<Stages...>` that computes each stage's divisor (3.3), at C7 | — |
| `guard_phase` | type | The form of the guard band stage's design (D37): the Chebyshev II a biquad-family design held by value, the type II FIR a table design (3.3, C3) | — |
| `guard_phase::nonlinear` | enumerator | The Chebyshev II form: the biquad family with its designs (C3, 3.3) | — |
| `guard_phase::linear` | enumerator | The type II FIR form: a direct-form FIR with a table design (C3, 3.3) | — |
| `half_band_kind` | type | Part of the key of the generated guard band designs, one per profile and half-band kind (3.3; 12) | — |
| `half_band_kind::fir` | enumerator | Part of the key of the generated guard band designs, one per profile and half-band kind (3.3; 12) | — |
| `half_band_kind::nonlinear` | enumerator | Part of the key of the generated guard band designs, one per profile and half-band kind (3.3; 12) | — |
| `half_band_kind::near_linear` | enumerator | Part of the key of the generated guard band designs, one per profile and half-band kind (3.3; 12) | — |
| `guard_band_settings` | type | The spec of the guard band design's `make()` (D37), and the key of its generated designs (3.3) | — |
| `guard_band_settings::passband_frac` | data member | The design's spec (D37) | — |
| `guard_band_settings::stopband_atten_db` | data member | The design's spec (D37) | — |
| `guard_band_settings::order` | data member | The section count (Chebyshev II) or the tap count (FIR) of the design type, a template argument (D37; 3.3, 7.1) | — |
| `guard_band_settings::super_economy(half_band_kind)`, `economy(...)`, `balanced(...)`, `transparent(...)` | member function | A generated design each, with a bit pin (3.3; 12, compile-time designs) | Run-time profile objects (3.3, CF) |
| `guard_band_settings::stop_edge()` | member function | A number the design stores (D37) | — |
| `k_guard_band_settings_reason` | constant | A `status` from the design's `make()` (D21, D37) | A string |
| `k_guard_band_linear_unmeetable_reason` | constant | — | A string from a run-time design search (3.3, CF) |
| `validate(const guard_band_settings&)` | function | The design's `make()`, returning `expected<T, status>` (D37, D21) | A `const char*` reason: failure is a `status` (D21) |
| `guard_band_filter_base<S, P>` | type | Nothing as a type: 7.1 shares code across channel counts by keeping storage in the template and arithmetic in a kernel over spans | A class that holds design, buffers and stream position together: configuration apart from state (D34) |
| `guard_band_filter_base::sample` | alias | The sample format of the stage's ports, `port<S, Ch>` (3.1, 3.2) | — |
| `guard_band_filter_base::state` (`tap::dsp::iir_state<S>`) | alias | Part of the stage's `state`, a plain aggregate (3.2, D34) | — |
| `guard_band_filter_base::coeff` | alias | The kernel's coefficient type the design holds (D37) | — |
| `guard_band_filter_base::table_type` | alias | The design's table in the kernel's format, over the carried biquad block kernel and `iir_traits` (12; D37) | — |
| `guard_band_filter_base::k_up` | constant | `rate_ratio`, `ratio{1, 1}` (3.2) | — |
| `guard_band_filter_base::k_down` | constant | `rate_ratio`, `ratio{1, 1}` (3.2) | — |
| `guard_band_filter_base::k_phase` | constant | The form of the guard band stage's design (D37): the Chebyshev II a biquad-family design held by value, the type II FIR a table design (3.3, C3) | — |
| `guard_band_filter_base::k_is_linear` | constant | The form of the guard band stage's design (D37): the Chebyshev II a biquad-family design held by value, the type II FIR a table design (3.3, C3) | — |
| `guard_band_filter_base::validate(const guard_band_settings&)` | member function | The design's `make()` (D37, D21) | A `const char*` reason: failure is a `status` (D21); for a custom linear setting, a run-time design search (3.3, CF) |
| `guard_band_filter_base::outputs_for(std::size_t)` | member function | `outputs_for(const state&, n)`, which is n: `input_group` 1 (3.2, D35) | — |
| `guard_band_filter_base::frames_needed(std::size_t)` | member function | `frames_needed(const state&, k)`, which is k (3.2) | — |
| `guard_band_filter_base::window_frames()` | member function | `tail_frames()`, with `flush_residue()` and `practical_tail_frames()` (3.2) | — |
| `guard_band_filter_base::flush_output_frames()` | member function | — | The flush is resumable: a call fills the buffer it is given and the flush is finished when `flush_remaining(state)` is 0 (D9, 4) |
| `guard_band_filter_base::reset()` | member function | `reset(state&) const noexcept`, the FIR form starting at its tap count less one (D49; 12) | — |
| `guard_band_filter_base::nominal_latency_output_frames()` | member function | `latency()`'s nominal (D39, 3.2) | — |
| `guard_band_filter_base::latency_deviation_output_frames()` | member function | `latency()`'s deviation (D39, 3.2) | — |
| `guard_band_filter_base::group_delay_output_frames(double)` | member function | `group_delay(f)`, a design-time free function (3.3) | As a member: `group_delay(f)` is a design-time free function, not a stage member (3.3) |
| `guard_band_filter_base::latency_output_frames()` | member function | `has_exact_latency` and `exact_latency`, (N - 1)/2 frames, a constant of the type (D39, 3.2) | — |
| `guard_band_filter_base::latency_seconds(double)` | member function | — | Latency is reported in output frames (D39, 3.2), and a port carries no rate (3.1) |
| `guard_band_filter_base::macs_per_output()` | member function | — | Not settled by the plan (Open: multiply counts) |
| `guard_band_filter_base::passband()` | member function | An accessor of the design (D37) | — |
| `guard_band_filter_base::stopband_db()` | member function | An accessor of the design (D37) | — |
| `guard_band_filter_base::stop_edge()` | member function | An accessor of the design (D37) | — |
| `guard_band_filter_base::order()` | member function | A constant of the design type (D37) | — |
| `guard_band_filter_base::tabulated()` | member function | — | A design comes from `make()` or the checked `from_table()` (D37); which one is known where it is made |
| `guard_band_filter_base::design()` | member function | The stage's `design()` accessor (3.2) | — |
| `guard_band_filter_base::table()` | member function | An accessor of the design's table (D37) | — |
| `guard_band_filter_base::states()` | member function | `stage_state<I>(state)` (4); the state is the caller's plain storage (D34) | — |
| `guard_band_filter_base::taps()` | member function | A constant of the Design type: a design that fixes a size is a type (D37) | — |
| `guard_band_filter_base::design_taps()` | member function | — | A `double` copy of the design: a design holds its coefficients in the kernel's type, and a stage built from a ROM design links no `double` (D37, 3.3) |
| `guard_band_filter_base::coefficients()` | member function | The design's `coefficients()` (9.2, D37) | — |
| `guard_band_filter<S, Ch, P>` | type | A 1:1 stage: the Chebyshev II on the biquad block kernel with a small design, the FIR a direct-form FIR with a table design (C3, 3.3), at C7 | An allocating constructor (D22) |
| `guard_band_filter::channels()` | member function | The `Ch` of the stage's ports, `port<S, Ch>` (3.1) | — |
| `guard_band_filter(const guard_band_settings&)` | member function | A configuration built from its design, which cannot fail (D37) | Run-time profile objects (3.3, CF); a custom linear setting's length search is a run-time design search (3.3, CF) |
| `guard_band_filter::process(const S*, std::size_t, S*)` | member function | The stage's trusted `run(state&, workspace&, in, out) const` (3.2), and the checked `process` of a chain of one (3.4, 4) | — |
| `guard_band_filter::pull(S*, std::size_t, PopFn&&)` | member function | `fed_by`, the exact pull from a junction (6.1, D52); from a source on the same thread, `frames_needed(state, k)` then `process` (3.2, 4) | A pull through a callback: the stage contract has no `pull` (3.2) |
| `guard_band_filter::flush(S*)` | member function | The resumable `flush(state&, workspace&, out)` of a chain of one (4, D9; 3.4), over the stage's `tail_frames()` (3.2) | — |
| `TAP_SR_RATIONAL_API` | macro | The export decoration of the C header the manifest expands, for the shared target `catenae_c` (7.5) and its exported-symbol pin (C6) | The engine's own spelling |
| `tap_sr_rational_converter` | C type | — | An opaque handle: nothing is chosen at run time behind a handle, and storage is the caller's, its size and alignment exported per target (D45, 7.5) |
| `TAP_SR_RATIONAL_UP2` | macro | The manifest entry for that stage, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_DOWN2` | macro | The manifest entry for that stage, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_UP3` | macro | The manifest entry for that stage, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_DOWN3` | macro | The manifest entry for that stage, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_RATIO3OVER2` | macro | The manifest entry for that stage, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_RATIO2OVER3` | macro | The manifest entry for that stage, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_RATIO4OVER3` | macro | The manifest entry for that stage, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_RATIO3OVER4` | macro | The manifest entry for that stage, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_UP2_UP2` | macro | The manifest entry for that chain, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_UP2_UP3` | macro | The manifest entry for that chain, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_UP2_UP6` | macro | The manifest entry for that chain, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_UP2_UP8` | macro | The manifest entry for that chain, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_UP2_RATIO4OVER3` | macro | The manifest entry for that chain, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_RATIO3OVER4_DOWN2` | macro | The manifest entry for that chain, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_DOWN2_DOWN2` | macro | The manifest entry for that chain, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_DOWN3_DOWN2` | macro | The manifest entry for that chain, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_DOWN6_DOWN2` | macro | The manifest entry for that chain, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_DOWN8_DOWN2` | macro | The manifest entry for that chain, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_UP2_UP4` | macro | The manifest entry for that chain, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_UP3_UP4` | macro | The manifest entry for that chain, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_UP2_UP4_UP2` | macro | The manifest entry for that chain, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_UP2_UP3_UP4` | macro | The manifest entry for that chain, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_UP2_UP4_UP4` | macro | The manifest entry for that chain, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_UP3_UP4_UP4` | macro | The manifest entry for that chain, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_DOWN4_DOWN2` | macro | The manifest entry for that chain, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_DOWN4_DOWN3` | macro | The manifest entry for that chain, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_DOWN2_DOWN4_DOWN2` | macro | The manifest entry for that chain, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_DOWN4_DOWN3_DOWN2` | macro | The manifest entry for that chain, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_DOWN4_DOWN4_DOWN2` | macro | The manifest entry for that chain, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_DOWN4_DOWN4_DOWN3` | macro | The manifest entry for that chain, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_UP2_RATIO3OVER2` | macro | The manifest entry for that chain, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_UP2_UP6_UP4` | macro | The manifest entry for that chain, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_DOWN4_DOWN6_DOWN2` | macro | The manifest entry for that chain, a named instantiation (D45, 7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_CHAIN_END` | macro | — | It bounds a run-time selector, and the manifest has none (D45) |
| `TAP_SR_RATIONAL_DOWN_BY4` | macro | The manifest entry of the chain it spells (7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_DOWN_BY8` | macro | The manifest entry of the chain it spells (7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_DOWN_BY16` | macro | The manifest entry of the chain it spells (7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_DOWN_BY16_TRANSPARENT` | macro | The manifest entry of the chain it spells (7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_UP_BY4` | macro | The manifest entry of the chain it spells (7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_UP_BY8` | macro | The manifest entry of the chain it spells (7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_UP_BY16` | macro | The manifest entry of the chain it spells (7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_UP_BY16_TRANSPARENT` | macro | The manifest entry of the chain it spells (7.5) | Its `int` value: nothing is chosen at run time behind a handle (D45) |
| `TAP_SR_RATIONAL_FORMAT_FLOAT` | macro | — | The entry fixes its format (7.5: what an entry fixes) |
| `TAP_SR_RATIONAL_FORMAT_Q15` | macro | — | The entry fixes its format (7.5: what an entry fixes) |
| `TAP_SR_RATIONAL_FORMAT_Q31` | macro | — | The entry fixes its format (7.5: what an entry fixes) |
| `TAP_SR_RATIONAL_DIRECTION_UP` | macro | — | The entry fixes its type-level choices: stage or junction type, format, channel count, design types (7.5) |
| `TAP_SR_RATIONAL_DIRECTION_DOWN` | macro | — | The entry fixes its type-level choices: stage or junction type, format, channel count, design types (7.5) |
| `TAP_SR_RATIONAL_IIR_NONLINEAR` | macro | — | The entry fixes its type-level choices: stage or junction type, format, channel count, design types (7.5) |
| `TAP_SR_RATIONAL_IIR_NEAR_LINEAR` | macro | — | The entry fixes its type-level choices: stage or junction type, format, channel count, design types (7.5) |
| `TAP_SR_RATIONAL_GUARD_NONLINEAR` | macro | — | The entry fixes its type-level choices: stage or junction type, format, channel count, design types (7.5) |
| `TAP_SR_RATIONAL_GUARD_LINEAR` | macro | — | The entry fixes its type-level choices: stage or junction type, format, channel count, design types (7.5) |
| `TAP_SR_RATIONAL_HALF_BAND_FIR` | macro | — | The entry fixes its type-level choices: stage or junction type, format, channel count, design types (7.5) |
| `TAP_SR_RATIONAL_HALF_BAND_NONLINEAR` | macro | — | The entry fixes its type-level choices: stage or junction type, format, channel count, design types (7.5) |
| `TAP_SR_RATIONAL_HALF_BAND_NEAR_LINEAR` | macro | — | The entry fixes its type-level choices: stage or junction type, format, channel count, design types (7.5) |
| `tap_sr_rational_create(int, int, unsigned)` | C function | A manifest entry per chain type, format, channel count and Designs (D45, 7.5) | A run-time choice of chain, profile and channel count behind a handle, and its fan-out of one-channel converters: run-time counts are outside the library (D45, D42) |
| `tap_sr_rational_create_format(int, int, int, unsigned)` | C function | A manifest entry per chain type, format, channel count and Designs (D45, 7.5) | A run-time choice of chain, profile, format and channel count behind a handle, and its fan-out (D45, D42) |
| `tap_sr_rational_create_stage(unsigned, unsigned, int, uint32_t, uint32_t, unsigned)` | C function | A manifest entry per stage type at a pinned design divisor (3.3, 7.5) | A stage created at an unpinned divisor (CF); a run-time ratio, profile and divisor behind a handle (D45) |
| `tap_sr_rational_create_stage_format(...)` | C function | A manifest entry per stage type and format at a pinned design divisor (3.3, 7.5) | A stage created at an unpinned divisor (CF); a run-time ratio, profile, divisor and format behind a handle (D45) |
| `tap_sr_rational_create_iir_format(int, unsigned, int, int, int, unsigned)` | C function | A manifest entry per recursive stage or named cascade, format and channel count (D45, 7.5) | A run-time choice of direction, factor, variant, profile and format behind a handle (D45) |
| `tap_sr_rational_create_guard_band_format(int, int, int, int, unsigned)` | C function | A manifest entry per guard band stage, design and format (D45, 7.5) | A run-time choice of form, half-band kind, profile and format behind a handle (D45) |
| `tap_sr_rational_destroy` | C function | — | No heap, and storage is the caller's (D22, D45) |
| `tap_sr_rational_ratio` | C function | A type-level fact of the entry, its `rate_ratio` (7.5, 3.2) | A run-time query of a handle (D45) |
| `tap_sr_rational_outputs_for` | C function | `outputs_for` as the entry's exported C function over the caller's storage (D45, 7.5) | — |
| `tap_sr_rational_frames_needed` | C function | `frames_needed` as the entry's exported C function over the caller's storage (D45, 7.5) | — |
| `tap_sr_rational_format` | C function | — | The entry fixes its format (7.5: what an entry fixes) |
| `tap_sr_rational_process`, `_q15`, `_q31` | C function | `process` as the entry's exported C function (D45, 7.5), a wrapper that returns a status for a wrong size (7.5), one entry per format | — |
| `tap_sr_rational_flush`, `_q15`, `_q31` | C function | The resumable `flush` (4, D9) as the entry's exported C function (D45, 7.5), a wrapper that returns a status for a wrong size (7.5), one entry per format | — |
| `tap_sr_rational_flush_output_frames` | C function | — | The flush is resumable: a call fills the buffer it is given and the flush is finished when `flush_remaining(state)` is 0 (D9, 4) |
| `tap_sr_rational_reset` | C function | The entry's `init(configuration, configuration_size, state, state_size)`, which is `reset` and checks the sizes, returning a status (D45, 7.5) | — |
| `tap_sr_rational_latency_output_frames` | C function | `exact_latency` (D39), where the entry `has_exact_latency`, as the entry's exported C function over the caller's storage (D45, 7.5) | The 0/0 refusal: an entry without an exact latency has none to export (D39) |
| `tap_sr_rational_latency_seconds` | C function | — | Latency is reported in output frames (D39, 3.2), and a port carries no rate (3.1) |
| `tap_sr_rational_nominal_latency_output_frames` | C function | `latency()`'s nominal as the entry's exported C function over the caller's storage (D45, 7.5) | — |
| `tap_sr_rational_group_delay_output_frames` | C function | `group_delay(f)`, a design-time free function (3.3), exported where a notebook needs it (7.5: the manifest is written per notebook; 11.1) | — |
| `tap_sr_rational_macs_per_output` | C function | — | Not settled by the plan (Open: multiply counts) |
| `tap_sr_rational_stages` | C function | A type-level fact of the entry (7.5) | A run-time query of a handle (D45) |
| `tap_sr_rational_stage_taps` | C function | A type-level fact of the entry, each Design's tap count (7.5, D37) | A run-time query of a handle (D45) |
| `tap_sr_rational_version` | C function | — | Not settled by the plan (Open: a C ABI version) |

## Open: members the plan does not place

| Question | Members | What it asks |
|---|---|---|
| A rate in Hz | async `config::sample_rate_hz`; async `config::for_sample_rate(double)`; async `servo_config::acquire_bandwidth_hz`; async `servo_config::track_bandwidth_hz`; async `servo_config::quiet_bandwidth_hz`; async `servo_config::acquire_smoother_hz`; async `servo_config::track_smoother_hz`; async `servo_config::quiet_smoother_hz`; async `servo_config::lock_hold_seconds`; async `servo_config::quiet_hold_seconds`; async `servo_config::scaled_to(double)`; async `filter_spec::passband_hz`; async `filter_spec::stopband_hz`; async `filter_spec::scaled_to(double)` | Does the `async` junction carry its nominal rate in Hz, with its servo's bandwidths and hold times and its bank's band edges in Hz and seconds, rescaled by `scaled_to` and `for_sample_rate`; or are they stated per frame (cycles per frame, frames), as a port, which carries no rate (3.1), would suggest? Section 12 says only that the servo is rewritten for the `float` image at C8, and 6.1 states latency in source frames. |
| The junction's target | async `config::target_latency_frames`; async `pi_servo::set_target(double)` | Is `async`'s target a template argument, as `handoff`'s `Target` is, so that `serves(largest_pull, quantum)` stays `static constexpr` and `fed_by` can assert it (6.1); or a configured number the junction raises from the pulls it sees, as SampleRateTap's `pull` does and as `latency(const state&)` allows (6.1: a junction that follows a clock moves its target as it runs)? With `fed_by`'s largest pull published at compile time (D52), is the run-time raise still needed? |
| The high-water resync | async `converter_status::resyncs` | Does `async` keep its hard resync at a high-water mark, the trim that section 13 defers for `handoff`, and if so is its count a field of `async`'s report type? 6.1 lists state, fill, underruns, overruns and a rate estimate. |
| The servo's rest | async `pi_servo::reset(bool)` | A junction's state rests at zero (D49, 6.1) and section 12 says `async` starts from its configured targets, but SampleRateTap's servo rests with its smoothers seeded at the target, which is not zero. Does an all-zero `async` state mean filling with the servo unseeded, the seed taken from the configuration on the pull that leaves filling; or does `async` need a `reset` that writes a non-zero rest, which D49 gives a stage and not a junction? |
| The channel-parallel layout | async `TAP_SR_ASYNC_CP_MIN_CHANNELS`; async `fractional_resampler::k_channel_parallel`; async `fractional_resampler::k_frame_major` | Is the history layout chosen by channel count (frame-major at or above a threshold, planar below) a build option, written into `catenae/config.h` and the inline namespace of 3.4, which today carry only the check level and the FFT choices; a template argument; or fixed per stage by its row in the layout benchmark (5.1)? |
| bridge's rate scale | bridge `ratio_traits::k_rate_scale`; bridge `ratio_traits::k_rate_factor`; bridge `basic_phase_table::k_rate_scale`; bridge `converter_base::k_rate_scale`; bridge `converter_to_96k<Ch>`; bridge `converter_to_88k2<Ch>`; bridge `converter_to_96k_q15<Ch>`; bridge `converter_to_88k2_q15<Ch>`; bridge `converter_to_96k_q31<Ch>`; bridge `converter_to_88k2_q31<Ch>`; bridge `converter_to_192k<Ch>`; bridge `converter_to_176k4<Ch>`; bridge `converter_to_192k_q15<Ch>`; bridge `converter_to_176k4_q15<Ch>`; bridge `converter_to_192k_q31<Ch>`; bridge `converter_to_176k4_q31<Ch>` | `bridge`'s K (the pair at 2x and 4x) states in the type which rates the frames carry, and its tables are bit for bit K = 0's. A catenae port carries no rate (3.1), so the 2x and 4x stages would be the same type as the base pair's. Are their names kept as aliases of that one type, or dropped? |
| Multiply counts | rational `stage_base::macs_per_superblock()`; rational `stage_base::macs_per_output()`; rational `basic_chain::macs_per_output_exact()`; rational `basic_chain::macs_per_output()`; rational `iir_stage_base::macs_per_output()`; rational `iir_stage_base::multiplies()`; rational `basic_iir_cascade::macs_per_output()`; rational `guard_band_filter_base::macs_per_output()`; rational `tap_sr_rational_macs_per_output` | Does a stage, a design or a chain publish its multiply count (the coverage-matrix test and the notebooks read `macs_per_output`, `macs_per_superblock`, `multiplies` and the C ABI's `tap_sr_rational_macs_per_output`), or does cost live only in the ratchet, the size probes and the cards (8, 11.2)? |
| The ratio as a type | rational `ratio<L, M>`; rational `is_ratio<T>` | Answered in part by CF finding F3: the `Ratio` argument of `stage<S, Ch, Ratio, Design>` (3.3) is spelled `catenae::ratio_t<L, M>` at C++17, where a class-type template parameter such as the value `ratio{up, down}` is not allowed (D2). Still open: is there a trait that recognises one? Does catenae keep `rational`'s charter (L and M of the form 2^a 3^b, lowest terms, L != M) as `static_assert`s on it, or only CAT-STAGE-006's reduced, non-zero `rate_ratio`, with the vocabulary bounded by the Design types that exist? |
| A near-linear delay path | rational `iir_stage_base::delay_path_output_frames()`; rational `basic_iir_cascade::delay_path_output_frames()` | A near-linear recursive stage or cascade has no exact latency (D39) but an exact delay path of whole frames, the number a caller's `delay` in `parallel<A, B, align::by_caller>` would need (D43). Does it publish that number, and as which member? |
| A C ABI version | async `tap_sr_async_version`; bridge `tap_sr_bridge_version`; rational `tap_sr_rational_version` | Does the C ABI manifest export a version? 7.5 makes each entry's `init` check the sizes it is given, to catch a header built for another target, level or back end, which covers what SampleRateTap's version probe guards; the plan names no version export. |
