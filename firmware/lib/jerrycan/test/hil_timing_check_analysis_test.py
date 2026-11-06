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
    """TIME_SYNC exchanges as analyze_exchanges takes them: (host_mid, board_mid, host_rtt, effective_rtt).

    `awake` < 1 is a counter that stops while the core sleeps; `steps` is (host seconds, size) jumps."""
    rng, out = random.Random(seed), []
    for i in range(int(seconds * rate)):
        t = i / rate
        board = 5.0 + t * awake * (1 + slope_ppm * 1e-6) + sum(size for at, size in steps if t >= at)
        slow = rng.random() < outliers
        rtt = 150e-6 + abs(rng.gauss(0, 10e-6)) + (0.004 if slow else 0.0)
        out.append((1e3 + t + rng.gauss(0, 20e-6), board + rng.gauss(0, 20e-6), rtt, rtt - 50e-6))
    return out


def phase(data):
    result = hil.analyze_exchanges(data)
    result.update(requests=len(data), answered=len(data))
    return result


def soak(idle=None, load=None, seconds=335):
    idle = idle if idle is not None else rows(seconds, 1.0)
    load = load if load is not None else rows(seconds, 50.0, seed=2)
    return {name: (ok, detail) for name, ok, detail in hil.judge_soak(phase(idle), phase(load))}


def failing(verdicts):
    return sorted(name for name, (ok, _) in verdicts.items() if not ok)


def test_good_clock_passes_everything():
    verdicts = soak(rows(335, 1.0, slope_ppm=25.0), rows(335, 50.0, slope_ppm=25.8, seed=2, outliers=0.02))
    assert failing(verdicts) == [], verdicts


def test_clock_that_loses_cycles_under_load_fails_the_slope_comparison_only():
    verdicts = soak(rows(335, 1.0, slope_ppm=25.0), rows(335, 50.0, slope_ppm=-25.0, seed=2))
    assert failing(verdicts) == ["soak_load_slope"], verdicts


def test_load_slope_limit_is_two_ppm():
    assert failing(soak(rows(335, 1.0, slope_ppm=25.0), rows(335, 50.0, slope_ppm=26.5, seed=2))) == []
    assert failing(soak(rows(335, 1.0, slope_ppm=25.0), rows(335, 50.0, slope_ppm=28.0, seed=2))) == ["soak_load_slope"]


def test_counter_that_stops_when_idle_fails_the_idle_slope():
    verdicts = soak(rows(335, 1.0, awake=0.02), rows(335, 50.0, seed=2))
    assert "soak_idle_slope" in failing(verdicts), verdicts
    assert "soak_wraps" in failing(verdicts)  # the idle board's clock also covers too few wraps


def test_lost_wrap_is_a_step_of_one_wrap():
    step = hil.WRAP_SECONDS
    verdicts = soak(load=rows(335, 50.0, steps=[(150.0, -step)], seed=2))
    assert "soak_steps" in failing(verdicts), verdicts  # the load slope fails too: the line is fitted across the step
    assert "whole number" in verdicts["soak_steps"][1]


def test_doubled_wrap_is_found_in_the_idle_phase_too():
    verdicts = soak(idle=rows(335, 1.0, steps=[(100.0, hil.WRAP_SECONDS), (200.0, hil.WRAP_SECONDS)]))
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
    noisy = [(h, b + random.Random(i).gauss(0, 400e-6), r, e) for i, (h, b, r, e) in enumerate(rows(335, 1.0))]
    verdicts = soak(idle=noisy)
    assert "soak_steps" in failing(verdicts) and "too high" in verdicts["soak_steps"][1], verdicts


def test_a_soak_that_is_too_short_fails():
    verdicts = soak(rows(200, 1.0), rows(200, 50.0, seed=2))
    assert failing(verdicts) == ["soak_wraps"], verdicts


def test_unanswered_requests_fail():
    idle, load = phase(rows(335, 1.0)), phase(rows(335, 50.0, seed=2))
    load["answered"] = int(0.5 * load["requests"])
    assert "soak_data" in [n for n, ok, _ in hil.judge_soak(idle, load) if not ok]


def test_monotonic_is_judged_per_type_and_kind():
    sampled, queued = hil.SAMPLED, hil.TX_QUEUE
    # A SAMPLED stamp older than the TX_QUEUE stamp before it is documented; within one kind it is not.
    frames = [(0x13, sampled, 1000, 7, 1), (0x13, queued, 1500, 7, 2), (0x13, sampled, 1100, 7, 3), (0x13, queued, 1600, 7, 4)]
    assert all(ok for _, ok, _ in hil.judge_ordering(frames, [(1, 10, 20), (2, 30, 40)], 7))
    frames.append((0x13, sampled, 1050, 7, 5))
    names = {n: ok for n, ok, _ in hil.judge_ordering(frames, [], 7)}
    assert not names["soak_monotonic"] and names["soak_boot_id"]


def test_boot_id_and_time_sync_order_are_checked():
    assert not dict((n, ok) for n, ok, _ in hil.judge_ordering([(0x20, 0, 5, 8, 1)], [], 7))["soak_boot_id"]
    assert hil.time_sync_order_violations([(1, 100, 110), (2, 120, 130)]) == \
        {"queued_before_received": 0, "receive_decreased": 0, "queue_decreased": 0}
    assert hil.time_sync_order_violations([(1, 100, 110), (2, 90, 130), (3, 140, 120)]) == \
        {"queued_before_received": 1, "receive_decreased": 1, "queue_decreased": 1}
    assert hil.time_sync_order_violations([(1, 100, 90)])["queued_before_received"] == 1


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
