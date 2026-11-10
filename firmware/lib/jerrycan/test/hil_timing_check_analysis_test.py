#!/usr/bin/env python3
"""Synthetic-data tests of hil_timing_check.py's soak and round-trip analysis.

Needs no board: python3 hil_timing_check_analysis_test.py. Each check must pass on a good clock and fail on the
fault it exists to catch, so every fault below is built in and its check asserted to fail.
"""

import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hil_timing_check as hil  # noqa: E402


def rows(seconds, rate, slope_ppm=25.0, steps=(), awake=1.0, outliers=0.0, seed=1):
    """Soak rows as analyze_exchanges takes them: (host_s, board_s, latency above the best).

    `awake` < 1 is a counter that stops while the core sleeps; `steps` is (host seconds, size) jumps."""
    rng, out = random.Random(seed), []
    for i in range(int(seconds * rate)):
        t = i / rate
        board = 5.0 + t * awake * (1 + slope_ppm * 1e-6) + sum(size for at, size in steps if t >= at)
        excess = abs(rng.gauss(0, 20e-6)) + (0.004 if rng.random() < outliers else 0.0)
        out.append((1e3 + t + rng.gauss(0, 10e-6), board + rng.gauss(0, 10e-6), excess))
    return out


def phase(data):
    result = hil.analyze_exchanges(data)
    result.update(requests=len(data), answered=len(data))
    return result


def soak(idle=None, load=None, seconds=335):
    idle = idle if idle is not None else rows(seconds, 5.0)
    load = load if load is not None else rows(seconds, 50.0, seed=2)
    return {name: (ok, detail) for name, ok, detail in hil.judge_soak(phase(idle), phase(load))}


def failing(verdicts):
    return sorted(name for name, (ok, _) in verdicts.items() if not ok)


def test_good_clock_passes_everything():
    verdicts = soak(rows(335, 5.0, slope_ppm=25.0), rows(335, 50.0, slope_ppm=25.8, seed=2, outliers=0.02))
    assert failing(verdicts) == [], verdicts


def test_clock_that_loses_cycles_under_load_fails_the_slope_comparison_only():
    verdicts = soak(rows(335, 5.0, slope_ppm=25.0), rows(335, 50.0, slope_ppm=-25.0, seed=2))
    assert failing(verdicts) == ["soak_load_slope"], verdicts


def test_load_slope_limit_is_two_ppm():
    assert failing(soak(rows(335, 5.0, slope_ppm=25.0), rows(335, 50.0, slope_ppm=26.5, seed=2))) == []
    assert failing(soak(rows(335, 5.0, slope_ppm=25.0), rows(335, 50.0, slope_ppm=28.0, seed=2))) == ["soak_load_slope"]


def test_counter_that_stops_when_idle_fails_the_idle_slope():
    verdicts = soak(rows(335, 5.0, awake=0.02), rows(335, 50.0, seed=2))
    assert "soak_idle_slope" in failing(verdicts), verdicts
    assert "soak_wraps" in failing(verdicts)  # the idle board's clock also covers too few wraps


def test_lost_wrap_is_a_step_of_one_wrap():
    verdicts = soak(load=rows(335, 50.0, steps=[(150.0, -hil.WRAP_SECONDS)], seed=2))
    assert "soak_steps" in failing(verdicts), verdicts  # the load slope fails too: the line is fitted across the step
    assert "whole number" in verdicts["soak_steps"][1]


def test_doubled_wrap_is_found_in_the_idle_phase_too():
    verdicts = soak(idle=rows(335, 5.0, steps=[(100.0, hil.WRAP_SECONDS), (200.0, hil.WRAP_SECONDS)]))
    assert "soak_steps" in failing(verdicts) and "idle" in verdicts["soak_steps"][1], verdicts


def test_one_millisecond_step_fails_and_a_smaller_one_is_only_noted():
    assert "soak_steps" in failing(soak(load=rows(335, 50.0, steps=[(150.0, 1.5e-3)], seed=2)))
    small = soak(load=rows(335, 50.0, steps=[(150.0, 0.4e-3)], seed=2))
    assert failing(small) == [] and "smaller jumps noted" in small["soak_steps"][1], small


def test_small_jumps_a_wrap_apart_fail():
    jumps = [(60.0, 0.3e-3), (60.0 + hil.WRAP_SECONDS, 0.3e-3)]
    verdicts = soak(load=rows(335, 50.0, steps=jumps, seed=2))
    assert failing(verdicts) == ["soak_steps"] and "wraps apart" in verdicts["soak_steps"][1], verdicts


def test_noise_too_high_to_see_a_millisecond_step_fails_rather_than_passes():
    noisy = [(h, b + random.Random(i).gauss(0, 400e-6), e) for i, (h, b, e) in enumerate(rows(335, 5.0))]
    verdicts = soak(idle=noisy)
    assert "soak_steps" in failing(verdicts) and "too high" in verdicts["soak_steps"][1], verdicts


def test_a_soak_that_is_too_short_fails():
    verdicts = soak(rows(200, 5.0), rows(200, 50.0, seed=2))
    assert failing(verdicts) == ["soak_wraps"], verdicts


def test_unanswered_requests_fail():
    idle, load = phase(rows(335, 5.0)), phase(rows(335, 50.0, seed=2))
    load["answered"] = int(0.5 * load["requests"])
    assert "soak_data" in [n for n, ok, _ in hil.judge_soak(idle, load) if not ok]


def test_responses_without_hardware_stamps_fail_the_data_check():
    idle, load = phase(rows(335, 5.0)), phase(rows(335, 50.0, seed=2))
    idle["counts"] = {"valid": 1675, "hardware": 0}
    load["counts"] = {"valid": 16750, "hardware": 16750}
    bad = {n: d for n, ok, d in hil.judge_soak(idle, load) if not ok}
    assert "soak_data" in bad and "hardware stamp" in bad["soak_data"], bad


# --- the PEAK card: no hardware stamp on echoes, a receive interrupt after 16 messages or 1 ms, software stamps taken in it ---

def card_answers(seconds, rate, ppm, seed, excursion=None):
    """TIME_SYNC responses as the card delivers them. Hardware stamps are exact (arrival on the wire, plus a little
    queueing); software stamps are batched into the interrupt; the request's echo has no hardware stamp. The first
    version of the soak paired echo and response by software stamps: both landed in one interrupt, every round trip
    read about 1 us, the effective round trip went negative and every row was dropped.

    `excursion` = (start_s, width_s, delta_us) shifts the board's clock by delta_us for that long, then back."""
    rnd = random.Random(seed)
    events = []  # (arrival_s, request_id, response_queue_us, request_receive_us)
    for i in range(int(seconds * rate)):
        tx = i / rate + rnd.random() * 20e-6
        rx_board = tx + 110e-6
        q = rx_board + rnd.uniform(12e-6, 40e-6)
        arrive = q + 150e-6 + abs(rnd.gauss(0, 15e-6)) + (rnd.random() < 0.05) * rnd.uniform(0.0, 0.0012)
        shift = excursion[2] if excursion and excursion[0] <= tx < excursion[0] + excursion[1] else 0
        events.append((arrive, i + 1, int(q * (1 + ppm * 1e-6) * 1e6) + shift, int(rx_board * (1 + ppm * 1e-6) * 1e6) + shift))
    background = [(k * 0.012 + j * 130e-6, 0, 0, 0) for k in range(int(seconds / 0.012)) for j in range(2)]
    stream = sorted(events + background)
    answers, i = {}, 0
    while i < len(stream):
        j, limit = i, stream[i][0] + 1e-3
        while j < len(stream) and stream[j][0] <= limit and j - i < 16:
            j += 1
        fire = (stream[j - 1][0] if j - i == 16 else limit) + 15e-6 + rnd.random() * 25e-6
        for k in range(i, j):
            arrive, rid, q_us, rx_us = stream[k]
            if rid:
                trailer = {"kind": hil.TX_QUEUE, "boot_id": 7, "board_us": q_us}
                answers[rid] = (rx_us, q_us, trailer, int((fire + (k - i) * 0.8e-6) * 1e9), int(arrive * 1e9) + 1)
        i = j
    return answers


def test_soak_works_with_interrupt_batched_software_stamps_and_no_echo_stamp():
    phases = {}
    for label, rate, seed in (("idle", 5.0, 1), ("load", 50.0, 2)):
        answers = card_answers(335, rate, 25.0, seed)
        points, counts = hil.response_points(answers, 7)
        assert counts == {"valid": len(answers), "hardware": len(answers)}
        phases[label] = phase(hil.one_way_rows(points))
        assert abs(phases[label]["slope_ppm"] - 25.0) < 0.05, (label, phases[label]["slope_ppm"])
        assert phases[label]["kept"] > 0.5 * len(answers), (label, phases[label]["kept"], len(answers))
    verdicts = {n: ok for n, ok, _ in hil.judge_soak(phases["idle"], phases["load"])}
    assert all(verdicts.values()), verdicts


def test_soak_ignores_software_stamps_entirely():
    answers = card_answers(335, 5.0, 25.0, 3)
    scrambled = {rid: (a[0], a[1], a[2], random.Random(rid).randrange(10**18), a[4]) for rid, a in answers.items()}
    good = hil.analyze_exchanges(hil.one_way_rows(hil.response_points(answers, 7)[0]))
    same = hil.analyze_exchanges(hil.one_way_rows(hil.response_points(scrambled, 7)[0]))
    assert good["slope_ppm"] == same["slope_ppm"]


def test_a_step_is_still_found_through_the_card():
    answers = card_answers(335, 50.0, 25.0, 4)
    stepped = {rid: (a[0], a[1] + (1500 if rid > 8000 else 0), {**a[2], "board_us": a[1] + (1500 if rid > 8000 else 0)},
                     a[3], a[4]) for rid, a in answers.items()}
    result = hil.analyze_exchanges(hil.one_way_rows(hil.response_points(stepped, 7)[0]))
    assert any(abs(s["step_us"]) > 1000 for s in result["steps"]), result["steps"]


def test_responses_without_a_hardware_stamp_give_no_points():
    answers = {rid: (a[0], a[1], a[2], a[3], 0) for rid, a in card_answers(20, 5.0, 25.0, 5).items()}
    points, counts = hil.response_points(answers, 7)
    assert points == [] and counts["hardware"] == 0 and counts["valid"] == len(answers)


def card_soak(excursion=None, seed=1):
    """Idle phase (optionally with a clock excursion) and a clean load phase, both through the card model."""
    idle = phase(hil.one_way_rows(hil.response_points(card_answers(335, 5.0, 25.0, seed, excursion), 7)[0]))
    load = phase(hil.one_way_rows(hil.response_points(card_answers(335, 50.0, 25.0, seed + 1), 7)[0]))
    return idle, load, {n: (ok, d) for n, ok, d in hil.judge_soak(idle, load)}


def test_a_clock_briefly_behind_is_the_one_way_fits_blind_spot_and_the_output_says_so():
    _, _, verdicts = card_soak((150.0, 3.0, -2000))
    assert failing(verdicts) == [], verdicts  # -2 ms for 3 s passes every check: lateness and a briefly-behind clock look alike
    assert "Limit of the one-way fit" in verdicts["soak_steps"][1] and "behind" in verdicts["soak_steps"][1]


def test_the_same_excursion_ahead_or_for_longer_is_caught():
    for excursion in ((150.0, 3.0, 2000), (150.0, 8.0, -2000), (150.0, 12.0, -2000)):
        assert "soak_steps" in failing(card_soak(excursion)[2]), excursion


def test_the_kept_floor_has_margin_under_a_healthy_board_and_catches_a_thin_fit():
    idle, load, verdicts = card_soak()
    kept = [phase_["kept"] / phase_["used"] for phase_ in (idle, load)]
    assert min(kept) > 0.9 and min(kept) > hil.MIN_KEPT_FRACTION + 0.1, kept  # the model with 5% of responses late
    assert failing(verdicts) == []
    thin = [(h, b, e if i % 4 == 0 else 0.002) for i, (h, b, e) in enumerate(rows(335, 5.0))]  # 3 in 4 are 2 ms late
    bad = {n: d for n, ok, d in hil.judge_soak(phase(thin), phase(rows(335, 50.0, seed=2))) if not ok}
    assert "soak_data" in bad and "rests on a minority" in bad["soak_data"] and "25%" in bad["soak_data"], bad


def stopped_in_wfi(answers, seed=5):
    """The board's clock runs only while the core is awake (an interrupt, the main thread, a 100 ms burst), each wake of
    jittered length: about 2% duty, and it scatters every point."""
    rnd, acc, last, out = random.Random(seed), 0.0, None, {}
    for rid in sorted(answers):
        a = answers[rid]
        t = a[4] / 1e9
        dt = 0.0 if last is None else t - last
        last = t
        acc += (dt / 0.012 * (60e-6 + rnd.uniform(-30e-6, 30e-6)) + dt / 0.1 * (200e-6 + rnd.uniform(-80e-6, 80e-6))
                + 100e-6 + rnd.uniform(-30e-6, 30e-6))
        q_us = int((5.0 + acc) * 1e6)
        out[rid] = (q_us - 20, q_us, {**a[2], "board_us": q_us}, a[3], a[4])
    return out


def test_a_counter_stopped_in_wfi_fails_its_own_check_and_not_only_the_data_check():
    idle = phase(hil.one_way_rows(hil.response_points(stopped_in_wfi(card_answers(335, 5.0, 25.0, 6)), 7)[0]))
    load = phase(hil.one_way_rows(hil.response_points(card_answers(335, 50.0, 25.0, 7), 7)[0]))
    assert idle["kept"] < hil.MIN_KEPT_FRACTION * idle["used"]  # the filter is scattered, as the review's model found (22%)
    failed = failing({n: (ok, d) for n, ok, d in hil.judge_soak(idle, load)})
    assert "soak_data" in failed and "soak_idle_slope" in failed, failed


def test_a_counter_stopped_so_hard_that_nothing_is_kept_still_has_a_slope_to_be_judged_by():
    answers = {rid: (a[0], 5000000, {**a[2], "board_us": 5000000}, a[3], a[4]) for rid, a in card_answers(335, 5.0, 25.0, 8).items()}
    idle = phase(hil.one_way_rows(hil.response_points(answers, 7)[0]))
    load = phase(hil.one_way_rows(hil.response_points(card_answers(335, 50.0, 25.0, 9), 7)[0]))
    failed = failing({n: (ok, d) for n, ok, d in hil.judge_soak(idle, load)})
    assert "soak_idle_slope" in failed, failed


# --- ordering ---

def test_monotonic_is_judged_per_type_and_kind():
    sampled, queued = hil.SAMPLED, hil.TX_QUEUE
    # A SAMPLED stamp older than the TX_QUEUE stamp before it is documented; within one kind it is not.
    frames = [(0x13, sampled, 1000, 7, 1, None), (0x13, queued, 1500, 7, 2, None), (0x13, sampled, 1100, 7, 3, None),
              (0x13, queued, 1600, 7, 4, None)]
    assert all(ok for _, ok, _ in hil.judge_ordering(frames, [(1, 10, 20), (2, 30, 40)], 7))
    frames.append((0x13, sampled, 1050, 7, 5, None))
    names = {n: ok for n, ok, _ in hil.judge_ordering(frames, [], 7)}
    assert not names["soak_monotonic"] and names["soak_boot_id"]


def pressure_stream(delta_us, phase_us, seconds=670):
    """The pellet's two pressure sensors: a 12 ms timer reads each ADC's latest completed sample; the ADCs complete every
    12.5 ms, offset from each other by delta_us. Each sensor's own stamps never go back; the two interleave."""
    frames, t, n = [], phase_us, 0
    while t < seconds * 1e6:
        for instance, offset in ((0, 0), (1, delta_us)):
            n += 1
            frames.append((0x08, hil.SAMPLED, int((t - offset) // 12500 * 12500 + offset), 7, n, instance))
        t += 12000
    return frames


def test_two_adcs_sampling_independently_are_two_streams_not_a_violation():
    for delta in (3, 100, 1000, -100):
        for phase_us in (17, 271, 488):
            frames = pressure_stream(delta, phase_us)
            shared = hil.monotonic_violations(((t, k), us) for t, k, us, _, _, _ in frames)
            assert shared, (delta, phase_us)  # keyed by type and kind alone this is certain to "fail"
            assert all(ok for _, ok, _ in hil.judge_ordering(frames, [], 7)), (delta, phase_us)


def test_a_real_regression_within_one_sensor_still_fails():
    frames = pressure_stream(100, 17)
    frames.append((0x08, hil.SAMPLED, 5, 7, 99999, 1))
    names = {n: d for n, ok, d in hil.judge_ordering(frames, [], 7) if not ok}
    assert "soak_monotonic" in names and "instance 1" in names["soak_monotonic"], names


def test_boot_id_and_time_sync_order_are_checked():
    assert not dict((n, ok) for n, ok, _ in hil.judge_ordering([(0x20, 0, 5, 8, 1, None)], [], 7))["soak_boot_id"]
    assert hil.time_sync_order_violations([(1, 100, 110), (2, 120, 130)]) == \
        {"queued_before_received": 0, "receive_decreased": 0, "queue_decreased": 0}
    assert hil.time_sync_order_violations([(1, 100, 110), (2, 90, 130), (3, 140, 120)]) == \
        {"queued_before_received": 1, "receive_decreased": 1, "queue_decreased": 1}
    assert hil.time_sync_order_violations([(1, 100, 90)])["queued_before_received"] == 1


# --- the period ruler ---

def gpio_frames(ppm, seconds=335, drop=0.0, seed=1):
    rng, frames, t, n = random.Random(seed), [], 1e6, 0
    while t < 1e6 + seconds * 1e6:
        n += 1
        if rng.random() >= drop:
            frames.append((0x0A, hil.SAMPLED, int(t + rng.gauss(0, 20)), 7, n, 0))
        t += 100000 * (1 + ppm * 1e-6)
    return frames


def test_period_ruler_reads_the_ppm_over_nominal():
    for ppm in (0.0, 38.0, -20.0):
        ruler = hil.period_ruler(gpio_frames(ppm))
        assert ruler["usable"] and abs(ruler["ppm_over_nominal"] - ppm) < 1.0, (ppm, ruler)
    ruler = hil.period_ruler(gpio_frames(38.0, drop=0.1))  # lost frames do not change the long-baseline reading
    assert abs(ruler["ppm_over_nominal"] - 38.0) < 1.0, ruler


def test_period_ruler_does_not_alias_at_the_rates_the_rig_has_recorded():
    # The rig's recordings read +322, +337, +384 and +538..+546 ppm. Over 335 s the span-based count wrapped every 298.6 ppm.
    for ppm in (322.0, 337.0, 384.0, 545.0, -200.0, 1200.0):
        for drop in (0.0, 0.1):
            ruler = hil.period_ruler(gpio_frames(ppm, drop=drop, seed=3))
            assert ruler["usable"] and abs(ruler["ppm_over_nominal"] - ppm) < 1.0, (ppm, drop, ruler)


def test_the_span_based_period_count_would_have_aliased():  # shows the test above can fail
    for ppm, printed in ((337.0, 38.2), (545.0, -52.5), (322.0, 23.2)):
        times = [f[2] / 1e6 for f in gpio_frames(ppm)]
        span = times[-1] - times[0]
        old = (span / (round(span / 0.1) * 0.1) - 1.0) * 1e6
        assert abs(old - printed) < 1.0 and abs(old - ppm) > 250, (ppm, old)


def test_period_ruler_refuses_what_is_not_a_100_ms_stream():
    assert hil.period_ruler(gpio_frames(0.0)[:10]) is None
    slow = [(0x0A, hil.SAMPLED, i * 250000, 7, i, 0) for i in range(100)]
    assert hil.period_ruler(slow)["usable"] is False


# --- round trips ---

def test_rtt_gate_catches_singles_the_fit_would_drop():
    burst = [120e-6, 130e-6, 125e-6, 140e-6]
    assert all(ok for _, ok, _ in hil.judge_rtt({"burst": burst, "single_1s": [150e-6] * 40, "single_30s": [400e-6] * 6}))
    slow = {name: ok for name, ok, _ in hil.judge_rtt({"burst": burst, "single_1s": [150e-6] * 40, "single_30s": [3e-3] * 6})}
    assert slow == {"rtt_gate_single_1s": True, "rtt_gate_single_30s": False}


def test_fit_helpers():
    xs = [i * 0.5 for i in range(100)]
    fit = hil.linear_fit(xs, [3.0 + 2.0 * x for x in xs])
    assert abs(fit["slope"] - 2.0) < 1e-12 and abs(fit["intercept"] - 3.0) < 1e-9
    stepped = [x * 1.00002 + (7.0 if x > 25 else 0.0) for x in xs]
    assert abs(hil.lagged_slope(xs, stepped, 4.0) - 1.00002) < 1e-9  # the step does not move it
    assert hil.summarize([3, 1, 2]) == {"n": 3, "min": 1, "median": 2, "p95": 2, "max": 3}


if __name__ == "__main__":
    tests = [(name, fn) for name, fn in sorted(globals().items()) if name.startswith("test_")]
    for name, fn in tests:
        fn()
        print(f"ok  {name}")
    print(f"{len(tests)} passed")
