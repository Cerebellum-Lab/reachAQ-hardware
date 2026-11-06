#!/usr/bin/env python3
"""Hardware-in-the-loop check of pellet-board timing. Development tool, not release tooling.

Run on the rig, with the pellet board on can0 running a CONFIG_LIB_JERRYCAN_TIMING build:

    python3 firmware/lib/jerrycan/test/hil_timing_check.py --channel can0 --address 0

It transmits CAPABILITIES_REQUEST and TIME_SYNC_REQUEST to the pellet, SYNC_CLAP
frames from --clapper-address (default 8, standing in for a timekeeper) unless a
real timekeeper is already clapping, and - only with --play-tone - one 200 ms
Tone 1, which is audible and drives STIM0.

Two opt-in modes run instead of those checks (each still starts with the capabilities check):

    --soak          the board clock against the host for 335 s idle, then 335 s under TIME_SYNC load
    --rtt-report    TIME_SYNC round trips for a burst of 4 and for singles 1 s and 30 s apart

Both send only TIME_SYNC_REQUEST, which the board answers and which changes nothing on it. --json PATH
writes every result for the record.
"""

import argparse
import bisect
import itertools
import json
import math
import socket
import statistics
import struct
import sys
import time

CAN_RAW_FD_FRAMES = 5
CAN_RAW_RECV_OWN_MSGS = 4
SO_TIMESTAMPING = 37
TIMESTAMPING_FLAGS = (1 << 2) | (1 << 3) | (1 << 4) | (1 << 6)  # RX hw, RX sw, sw report, raw hw report
CANFD_BRS = 0x01
CAN_ERR_FLAG = 0x20000000
FRAME = struct.Struct("<IBBBB64s")
TRAILER = struct.Struct("<BBBBIIQ")
FD_LENGTHS = (0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64)

CAPABILITIES_REQUEST, CAPABILITIES_RESPONSE = 0x23, 0x24
TIME_SYNC_REQUEST, TIME_SYNC_RESPONSE = 0x1F, 0x20
SYNC_CLAP, SYNC_CLAP_OBSERVED = 0x25, 0x26
TONE = 0x0C
# Board-to-host payload sizes, so the trailer can be found after payload + uuid.
PAYLOAD_SIZES = {0x01: 8, 0x07: 28, 0x08: 5, 0x0A: 5, 0x0C: 5, 0x0F: 1, 0x13: 3, 0x16: 12, 0x17: 6,
                 0x19: 7, 0x20: 20, 0x22: 12, 0x24: 9, 0x26: 5, 0x30: 4, 0x3F: 1}
TX_QUEUE, OBSERVED, PHYSICAL_START, SAMPLED = 0, 1, 3, 5
REQUIRED_CAPABILITIES = 0b10011  # timing_trailer | time_sync | sync_listener

results = []
report_log = []


def report(name, ok, detail):
    results.append(ok)
    report_log.append({"name": name, "ok": ok, "detail": detail})
    print(f"[{'PASS' if ok else 'FAIL'}] {name}: {detail}")


def open_socket(channel):
    s = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    s.setsockopt(socket.SOL_CAN_RAW, CAN_RAW_FD_FRAMES, 1)
    s.setsockopt(socket.SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, 1)
    s.setsockopt(socket.SOL_SOCKET, SO_TIMESTAMPING, TIMESTAMPING_FLAGS)
    s.bind((channel,))
    s.settimeout(0.05)
    return s


def send(s, msg_type, address, payload, uuid=0):
    data = payload + bytes([uuid])
    length = next(n for n in FD_LENGTHS if n >= len(data))
    s.send(FRAME.pack((msg_type << 5) | address, length, CANFD_BRS, 0, 0, data.ljust(64, b"\0")))


def decode_stamped(raw, ancillary, flags):
    """One received frame as (can_id, data, own, timestamp_ns, hardware, software_ns, hardware_ns), or None for
    an error frame. timestamp_ns is the hardware stamp when there is one, the kernel's software stamp if not."""
    can_id, length, _, _, _, data = FRAME.unpack(raw.ljust(FRAME.size, b"\0"))
    if can_id & CAN_ERR_FLAG:
        return None
    sw_ns = hw_ns = 0
    for level, kind, value in ancillary:
        if level == socket.SOL_SOCKET and kind == SO_TIMESTAMPING and len(value) >= 48:
            sw_s, sw_n, _, _, hw_s, hw_n = struct.unpack_from("<qqqqqq", value)
            sw_ns, hw_ns = sw_s * 10**9 + sw_n, hw_s * 10**9 + hw_n
    # MSG_CONFIRM marks frames this socket sent. MSG_DONTROUTE would also match
    # frames from any other local process, such as a running reachAQ.
    return (can_id & 0x7FF, data[:length], bool(flags & socket.MSG_CONFIRM), hw_ns or sw_ns, hw_ns != 0, sw_ns, hw_ns)


def decode_message(raw, ancillary, flags):
    """One received frame as (can_id, data, own, timestamp_ns, hardware), or None for an error frame."""
    message = decode_stamped(raw, ancillary, flags)
    return None if message is None else message[:5]


def receive(s, seconds):
    """Yield (can_id, data, own, timestamp_ns, hardware) for `seconds`."""
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            raw, ancillary, flags, _ = s.recvmsg(FRAME.size, 256)
        except socket.timeout:
            continue
        message = decode_message(raw, ancillary, flags)
        if message is not None:
            yield message


def trailer(msg_type, data):
    size = PAYLOAD_SIZES.get(msg_type)
    if size is None or len(data) < size + 1 + TRAILER.size:
        return None
    tag, version, kind, flags, boot_id, sequence, board_us = TRAILER.unpack_from(data, size + 1)
    if tag != 0xA7 or version != 1:
        return None
    return {"kind": kind, "flags": flags, "boot_id": boot_id, "sequence": sequence, "board_us": board_us}


def check_capabilities(s, address):
    send(s, CAPABILITIES_REQUEST, address, b"\0")
    for can_id, data, own, _, _ in receive(s, 1.0):
        if not own and can_id == (CAPABILITIES_RESPONSE << 5) | address:
            _, caps, boot_id = struct.unpack_from("<BII", data)
            t = trailer(CAPABILITIES_RESPONSE, data)
            ok = caps & REQUIRED_CAPABILITIES == REQUIRED_CAPABILITIES and t is not None and t["boot_id"] == boot_id
            report("capabilities", ok, f"caps=0x{caps:08x} boot_id=0x{boot_id:08x} trailer={'yes' if t else 'no'}")
            # Stop here on a failure: a board without the timing features gets no further traffic.
            return boot_id if ok else None
    report("capabilities", False, "no CAPABILITIES_RESPONSE within 1 s")
    return None


def check_time_sync(s, address, boot_id, count=20):
    # A distinctive id range, so replies to reachAQ's own requests (ids from 1) are not counted
    base = 0x7A000000
    sent, answers = {}, {}
    for request_id in range(base + 1, base + count + 1):
        send(s, TIME_SYNC_REQUEST, address, struct.pack("<IQ", request_id, time.perf_counter_ns()))
        for can_id, data, own, ts, hw in receive(s, 0.05):
            if own and can_id == (TIME_SYNC_REQUEST << 5) | address:
                sent[struct.unpack_from("<I", data)[0]] = (ts, hw)
            elif not own and can_id == (TIME_SYNC_RESPONSE << 5) | address:
                rid, received_us, queued_us = struct.unpack_from("<IQQ", data)
                answers[rid] = (received_us, queued_us, trailer(TIME_SYNC_RESPONSE, data), ts, hw)
    # The trailer must say the response was stamped when queued, at the time the payload reports
    good = [rid for rid, (rx, q, t, _, _) in answers.items()
            if rid > base and t is not None and t["boot_id"] == boot_id and t["kind"] == TX_QUEUE
            and t["board_us"] == q and rx <= q]
    # A round trip is only meaningful when both ends were stamped by the same clock
    paired = [r for r in good if r in sent]
    sources = {sent[r][1] for r in paired} | {answers[r][4] for r in paired}
    if not paired:
        rtt = "RTT not computed: no valid response matched a request echo"
    elif len(sources) > 1:
        rtt = "RTT not computed: mixed hardware/software stamps"
    else:
        rtts = sorted((answers[r][3] - sent[r][0]) / 1000 for r in paired)
        rtt = f"round trip min {rtts[0]:.0f} us ({'hardware' if True in sources else 'software'} stamps)"
    board_us = sorted(answers[r][1] - answers[r][0] for r in good)
    ok = len(good) >= count - 1
    report("time_sync", ok, f"{len(good)}/{count} valid (trailer TX_QUEUE at response_queue_time_us); {rtt}, "
           f"board handling median {board_us[len(board_us) // 2] if board_us else float('nan')} us")


def other_clappers(s, seconds=1.5):
    """Addresses that sent SYNC_CLAP within `seconds`, listening only."""
    return sorted({can_id & 0x1F for can_id, _, own, _, _ in receive(s, seconds)
                   if not own and can_id >> 5 == SYNC_CLAP})


def check_claps(s, address, boot_id, clapper_address, count=50):
    # Fake claps beside a real timekeeper's would corrupt every board's clap fit, so listen first
    clappers = other_clappers(s)
    if clappers:
        report("claps", False, "SKIPPED, no claps sent: a timekeeper is already clapping on this bus "
               f"(SYNC_CLAP from address {', '.join(str(a) for a in clappers)})")
        return
    host_ts, board_us, hardware, bad = {}, {}, True, 0
    for tick in range(1, count + 1):
        send(s, SYNC_CLAP, clapper_address, struct.pack("<I", tick))
        for can_id, data, own, ts, hw in receive(s, 0.1):
            if own and can_id == (SYNC_CLAP << 5) | clapper_address:
                host_ts[struct.unpack_from("<I", data)[0]] = ts
                hardware = hardware and hw
            elif not own and can_id == (SYNC_CLAP_OBSERVED << 5) | address:
                heard, clapper = struct.unpack_from("<IB", data)
                t = trailer(SYNC_CLAP_OBSERVED, data)
                if t is None or clapper != clapper_address or t["kind"] != OBSERVED or t["boot_id"] != boot_id:
                    bad += 1
                else:
                    board_us[heard] = t["board_us"]
    pairs = [(host_ts[k] / 1e9, board_us[k] / 1e6) for k in board_us if k in host_ts]
    if len(pairs) < 3:
        report("claps", False, f"only {len(pairs)} clap(s) matched")
        return
    n = len(pairs)
    mx, my = sum(x for x, _ in pairs) / n, sum(y for _, y in pairs) / n
    slope = sum((x - mx) * (y - my) for x, y in pairs) / sum((x - mx) ** 2 for x, _ in pairs)
    residual_us = sorted(abs(y - (my + slope * (x - mx))) * 1e6 for x, y in pairs)
    p95 = residual_us[int(0.95 * (n - 1))]
    # The acceptance is p95 < 20 us against hardware stamps; software stamps cannot show it
    if hardware:
        jitter_ok = p95 < 20.0
        jitter = f"jitter judged (hardware timestamps): p95 {'<' if jitter_ok else '>='} 20 us"
    else:
        jitter_ok = False
        jitter = "jitter NOT judged (kernel software timestamps), so the hardware acceptance is not met"
    ok = n >= count - 2 and bad == 0 and abs(slope - 1.0) < 1e-3 and jitter_ok
    report("claps", ok, f"{n}/{count} observed, {bad} malformed, slope {slope:.9f}, residual p95 {p95:.1f} us, "
           f"max {residual_us[-1]:.1f} us; {jitter}")


def check_periodic_trailers(s, address, boot_id):
    kinds, sequences = {}, []
    for can_id, data, own, _, _ in receive(s, 2.0):
        msg_type, sender = can_id >> 5, can_id & 0x1F
        if own or sender != address:
            continue
        t = trailer(msg_type, data)
        if t is None or t["boot_id"] != boot_id:
            continue
        sequences.append(t["sequence"])
        kinds.setdefault(msg_type, set()).add(t["kind"])
    gaps = sum(1 for a, b in zip(sequences, sequences[1:]) if b != a + 1)
    want = {0x08: SAMPLED, 0x0A: SAMPLED, 0x0F: SAMPLED}
    missing = [f"0x{m:02x}" for m, k in want.items() if k not in kinds.get(m, set())]
    report("periodic_trailers", not missing and gaps == 0,
           f"{len(sequences)} trailer frames, {gaps} sequence gap(s), missing SAMPLED on {missing or 'none'}")


def check_tone(s, address):
    send(s, TONE, address, struct.pack("<BHH", 0, 5000, 200), uuid=0x5A)
    for can_id, data, own, _, _ in receive(s, 0.5):
        if not own and can_id == (TONE << 5) | address:
            t = trailer(TONE, data)
            if t is not None and t["kind"] == PHYSICAL_START:
                frequency = struct.unpack_from("<BHH", data)[1]
                report("tone_physical_start", frequency == 5000, f"PHYSICAL_START at {t['board_us']} us, {frequency} Hz")
                return
    report("tone_physical_start", False, "no TONE status with PHYSICAL_START within 0.5 s")


# --- Clock soak and round-trip report -------------------------------------------------------------
# Opt-in (--soak, --rtt-report); the default run above is unchanged. The analysis is in pure functions,
# tested on synthetic data by hil_timing_check_analysis_test.py. The collection that feeds them needs the board.

BOARD_CLOCK_HZ = 170e6
WRAP_SECONDS = 2 ** 32 / BOARD_CLOCK_HZ  # CYCCNT is 32 bits: it wraps every 25.2645 s at 170 MHz
STEP_FAIL_S = 1e-3  # a step this large from the line fails the soak
STEP_NOTE_S = 100e-6  # a jump this large is reported; two of them a wrap apart fail the soak
LOAD_SLOPE_LIMIT_PPM = 2.0  # idle and load slopes may differ by this much
SLOPE_SANITY_PPM = 500.0  # the board's crystal and the adapter's agree to ~100 ppm; a counter stopped in WFI reads far from 1
MIN_SOAK_SPAN_S = 330.0  # 5.5 min, which holds 13 wraps
MIN_ANSWERED = 0.9  # fraction of TIME_SYNC requests that must be answered
RTT_WINDOW_S = 0.002  # reachAQ's BoardClockModel._fit keeps exchanges within this of the best round trip
KIND_NAMES = {0: "TX_QUEUE", 1: "OBSERVED", 2: "COMMAND_RECEIVED", 3: "PHYSICAL_START", 4: "COMPLETED", 5: "SAMPLED"}


def percentile(sorted_values, q):
    return sorted_values[int(q * (len(sorted_values) - 1))]


def summarize(values):
    ordered = sorted(values)
    if not ordered:
        return None
    return {"n": len(ordered), "min": ordered[0], "median": statistics.median(ordered),
            "p95": percentile(ordered, 0.95), "max": ordered[-1]}


def linear_fit(xs, ys):
    """Least-squares line y = intercept + slope * x."""
    n = len(xs)
    if n < 2:
        raise ValueError("a line needs at least 2 points")
    mx, my = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    if sxx == 0:
        raise ValueError("all points have the same x")
    slope = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx
    return {"slope": slope, "intercept": my - slope * mx, "n": n}


def line_residuals(xs, ys, fit):
    return [y - (fit["intercept"] + fit["slope"] * x) for x, y in zip(xs, ys)]


def lagged_slope(xs, ys, lag):
    """Median slope between each point and the first one at least `lag` later (xs ascending).

    A step corrupts only the pairs that straddle it, so unlike a least-squares slope this one ignores a few."""
    slopes, j, n = [], 0, len(xs)
    for i in range(n):
        j = max(j, i + 1)
        while j < n and xs[j] - xs[i] < lag:
            j += 1
        if j >= n:
            break
        slopes.append((ys[j] - ys[i]) / (xs[j] - xs[i]))
    return statistics.median(slopes) if slopes else None


def _largest(group):
    x, step = max(group, key=lambda item: abs(item[1]))
    return {"x": x, "step_s": step}


def find_steps(xs, ys, window=4.0, grid=1.0, min_points=3, report_s=STEP_NOTE_S):
    """Jumps of y - x between adjacent time windows, once the drift is taken out.

    Compares the median of each `window` seconds before a boundary with the median after it, at a boundary every
    `grid` seconds. Neither a drifting crystal nor a few bad points moves a median, and a whole-wrap step (25 s)
    shows at its full size, which a least-squares line cannot show. Returns {"steps": [{"x", "step_s"}] for each
    jump of at least `report_s` (raised to 6 sigma of the medians' own noise, so noise is not reported as steps),
    "max_abs_s": the largest jump seen, "noise_s": point-to-point noise, "floor_s": the smallest jump reported}."""
    result = {"steps": [], "max_abs_s": 0.0, "noise_s": 0.0, "floor_s": report_s}
    n = len(xs)
    if n < 2 * min_points or xs[-1] - xs[0] < 2 * window:
        return result
    z = [y - x for x, y in zip(xs, ys)]
    drift = lagged_slope(xs, z, min(10.0, (xs[-1] - xs[0]) / 4.0)) or 0.0
    e = [zi - drift * (x - xs[0]) for x, zi in zip(xs, z)]
    result["noise_s"] = 1.4826 * statistics.median(abs(b - a) for a, b in zip(e, e[1:])) / math.sqrt(2.0)
    per_window = max(min_points, int(window * (n - 1) / (xs[-1] - xs[0])))
    report_s = result["floor_s"] = max(report_s, 6 * 1.2533 * result["noise_s"] * math.sqrt(2.0 / per_window))
    candidates = []
    boundary = xs[0] + window
    while boundary <= xs[-1] - window + 1e-9:
        lo = bisect.bisect_left(xs, boundary - window)
        mid = bisect.bisect_left(xs, boundary)
        hi = bisect.bisect_left(xs, boundary + window)
        if mid - lo >= min_points and hi - mid >= min_points:
            candidates.append((boundary, statistics.median(e[mid:hi]) - statistics.median(e[lo:mid])))
        boundary += grid
    result["max_abs_s"] = max((abs(d) for _, d in candidates), default=0.0)
    group = []
    for boundary, d in candidates:
        if abs(d) >= report_s and group and (d > 0) == (group[-1][1] > 0) and boundary - group[-1][0] <= 1.5 * grid:
            group.append((boundary, d))
            continue
        if group:
            result["steps"].append(_largest(group))
        group = [(boundary, d)] if abs(d) >= report_s else []
    if group:
        result["steps"].append(_largest(group))
    return result


def wrap_related_steps(steps, wrap_s=WRAP_SECONDS, size_tolerance=0.02, spacing_tolerance_s=1.5):
    """Steps that are a whole number of wraps in size (a lost or doubled wrap), and pairs of steps a whole
    number of wraps apart (something that happens once per wrap)."""
    sized, spaced = [], []
    for step in steps:
        k = round(abs(step["step_s"]) / wrap_s)
        if k >= 1 and abs(abs(step["step_s"]) - k * wrap_s) <= size_tolerance * wrap_s:
            sized.append(step)
    for a, b in itertools.combinations(steps, 2):
        gap = abs(b["x"] - a["x"])
        k = round(gap / wrap_s)
        if k >= 1 and abs(gap - k * wrap_s) <= spacing_tolerance_s:
            spaced.append((a, b))
    return {"wrap_sized": sized, "wrap_spaced": spaced}


def analyze_exchanges(rows, rtt_slack_s=0.001):
    """One soak phase. `rows` are (host_mid_s, board_mid_s, host_rtt_s, effective_rtt_s) per TIME_SYNC exchange,
    all stamped by the same host clock. Exchanges slower than the best by more than `rtt_slack_s` are dropped,
    as reachAQ's fit does, then the board-against-host line is fitted and checked for steps."""
    out = {"used": len(rows)}
    sane = [r for r in rows if r[3] >= 0.0]
    if len(sane) < 10:
        out["error"] = f"only {len(sane)} exchanges with a usable round trip"
        return out
    out["host_rtt_us"] = _micro(summarize([r[2] for r in sane]))
    out["effective_rtt_us"] = _micro(summarize([r[3] for r in sane]))
    best = min(r[3] for r in sane)
    kept = sorted((r for r in sane if r[3] <= best + rtt_slack_s), key=lambda r: r[0])
    out["kept"] = len(kept)
    if len(kept) < 10:
        out["error"] = f"only {len(kept)} exchanges within {rtt_slack_s * 1e3:g} ms of the best round trip"
        return out
    x0, y0 = kept[0][0], kept[0][1]
    xs = [r[0] - x0 for r in kept]
    ys = [r[1] - y0 for r in kept]
    fit = linear_fit(xs, ys)
    residual = line_residuals(xs, ys, fit)
    magnitude = sorted(abs(r) for r in residual)
    robust = lagged_slope(xs, ys, min(10.0, xs[-1] / 4.0))
    found = find_steps(xs, ys)
    out.update({
        "host_span_s": xs[-1], "board_span_s": ys[-1], "wraps": ys[-1] / WRAP_SECONDS,
        "slope_ppm": (fit["slope"] - 1.0) * 1e6,
        "robust_slope_ppm": None if robust is None else (robust - 1.0) * 1e6,
        "residual_rms_us": math.sqrt(sum(r * r for r in residual) / len(residual)) * 1e6,
        "residual_p95_us": percentile(magnitude, 0.95) * 1e6, "residual_max_us": magnitude[-1] * 1e6,
        "noise_us": found["noise_s"] * 1e6, "max_step_us": found["max_abs_s"] * 1e6,
        "step_floor_us": found["floor_s"] * 1e6,
    })
    related = wrap_related_steps(found["steps"])
    sized = {s["x"] for s in related["wrap_sized"]}
    out["steps"] = [{"at_s": s["x"], "step_us": s["step_s"] * 1e6, "wrap_sized": s["x"] in sized} for s in found["steps"]]
    out["wrap_spaced"] = [(a["x"], b["x"]) for a, b in related["wrap_spaced"]]
    return out


def _micro(stats):
    return None if stats is None else {k: (v if k == "n" else v * 1e6) for k, v in stats.items()}


def _fmt_us(stats):
    if stats is None:
        return "none"
    return f"n={stats['n']} min {stats['min']:.0f} median {stats['median']:.0f} p95 {stats['p95']:.0f} max {stats['max']:.0f} us"


def monotonic_violations(records):
    """records: (key, value) in arrival order. A value below its key's previous one is a violation.
    Returns {key: {"count", "worst"}}, where worst is the largest backward step."""
    last, bad = {}, {}
    for key, value in records:
        previous = last.get(key)
        if previous is not None and value < previous:
            entry = bad.setdefault(key, {"count": 0, "worst": 0})
            entry["count"] += 1
            entry["worst"] = max(entry["worst"], previous - value)
        last[key] = value
    return bad


def time_sync_order_violations(rows):
    """rows: (request_id, request_receive_us, response_queue_us). Receive stamps happen in arrival order and queue
    stamps in processing order, so each must not decrease across requests, and a reply is never queued before
    its request arrived."""
    ordered = sorted(rows)
    return {
        "queued_before_received": sum(1 for _, rx, q in ordered if q < rx),
        "receive_decreased": sum(1 for a, b in zip(ordered, ordered[1:]) if b[1] < a[1]),
        "queue_decreased": sum(1 for a, b in zip(ordered, ordered[1:]) if b[2] < a[2]),
    }


def _ppm(value):
    return "n/a" if value is None else f"{value:+.3f} ppm"


def _describe_phase(label, a):
    if "error" in a:
        return f"{label}: {a['error']}"
    return (f"{label}: {a['kept']}/{a['used']} exchanges kept, board span {a['board_span_s']:.1f} s "
            f"({a['wraps']:.1f} wraps), slope 1{_ppm(a['slope_ppm'])} (lag-median 1{_ppm(a['robust_slope_ppm'])}), "
            f"residual rms {a['residual_rms_us']:.1f} us p95 {a['residual_p95_us']:.1f} us max {a['residual_max_us']:.1f} us, "
            f"noise {a['noise_us']:.1f} us, largest step {a['max_step_us']:.1f} us")


def judge_soak(idle, load, min_span_s=MIN_SOAK_SPAN_S, limit_ppm=LOAD_SLOPE_LIMIT_PPM, step_fail_s=STEP_FAIL_S):
    """The soak's verdicts as (name, ok, detail). `idle` and `load` come from analyze_exchanges, plus the
    caller's 'requests' and 'answered' counts."""
    phases = (("idle", idle), ("load", load))
    checks = []

    short = [f"{label} {a['answered']}/{a['requests']} answered" for label, a in phases
             if a.get("requests") and a.get("answered", 0) < MIN_ANSWERED * a["requests"]]
    broken = [f"{label}: {a['error']}" for label, a in phases if "error" in a]
    if len({a.get("source") for _, a in phases}) > 1:
        broken.append("the phases were stamped by different clocks (" + ", ".join(
            f"{label} {a.get('source')}" for label, a in phases) + "), so their slopes cannot be compared")
    software = any(a.get("source") == "software" for _, a in phases)
    checks.append(("soak_data", not short and not broken,
                   "; ".join(short + broken) or ", ".join(
                       f"{label} {a.get('answered', '?')}/{a.get('requests', '?')} answered, {a['kept']} used"
                       + (f", {a['source']} stamps" if a.get("source") else "") for label, a in phases)
                   + ("; kernel software stamps, so the host clock is CLOCK_REALTIME and NTP slewing enters both slopes"
                      if software else "")))
    if broken:
        return checks  # nothing below can be judged

    spans = [(label, a["board_span_s"], a["wraps"]) for label, a in phases]
    checks.append(("soak_wraps", all(span >= min_span_s for _, span, _ in spans),
                   "; ".join(f"{label} {span:.1f} s = {wraps:.1f} wraps" for label, span, wraps in spans)
                   + f" (need {min_span_s:.0f} s each, 13 wraps of {WRAP_SECONDS:.2f} s)"))

    problems, notes = [], []
    for label, a in phases:
        if a["step_floor_us"] > 0.5 * step_fail_s * 1e6:
            problems.append(f"{label}: noise {a['noise_us']:.0f} us is too high to resolve a {step_fail_s * 1e3:g} ms step "
                            f"(smallest step it could show is {a['step_floor_us']:.0f} us)")
        for s in a["steps"]:
            text = f"{label}: {s['step_us']:+.1f} us at {s['at_s']:.0f} s"
            if abs(s["step_us"]) >= step_fail_s * 1e6:
                problems.append(f"step {text}" + (f" (a whole number of {WRAP_SECONDS:.2f} s wraps)" if s["wrap_sized"] else ""))
            else:
                notes.append(text)
        problems += [f"{label}: jumps at {x:.0f} s and {y:.0f} s are a whole number of wraps apart" for x, y in a["wrap_spaced"]]
    checks.append(("soak_steps", not problems,
                   "; ".join(problems) or f"no step of {step_fail_s * 1e3:g} ms or more, none a whole wrap in size or spacing; "
                   f"largest idle {idle['max_step_us']:.1f} us, load {load['max_step_us']:.1f} us"
                   + (f"; smaller jumps noted: {'; '.join(notes)}" if notes else "")))

    stepped = " (the fit runs across the steps soak_steps found)" if any(
        abs(s["step_us"]) >= step_fail_s * 1e6 for _, a in phases for s in a["steps"]) else ""
    checks.append(("soak_idle_slope", abs(idle["slope_ppm"]) <= SLOPE_SANITY_PPM,
                   f"idle slope 1{_ppm(idle['slope_ppm'])} against the host over {idle['board_span_s']:.0f} s with the board "
                   f"mostly idle; within {SLOPE_SANITY_PPM:.0f} ppm of 1 shows the counter runs in WFI (stopped in it, the slope would be far from 1)" + stepped))
    difference = load["slope_ppm"] - idle["slope_ppm"]
    checks.append(("soak_load_slope", abs(difference) <= limit_ppm,
                   f"idle 1{_ppm(idle['slope_ppm'])}, load 1{_ppm(load['slope_ppm'])}, difference {difference:+.3f} ppm (limit {limit_ppm:g}); "
                   f"residual rms idle {idle['residual_rms_us']:.1f} us, load {load['residual_rms_us']:.1f} us" + stepped))
    return checks


def kind_name(kind):
    return KIND_NAMES.get(kind, str(kind))


def judge_ordering(frames, sync_rows, boot_id):
    """frames: (msg_type, kind, board_us, boot_id, sequence) for every trailer-bearing frame the board sent, in
    arrival order. Stamps are compared per message type and per kind: a SAMPLED stamp is older than the
    TX_QUEUE stamp of a frame queued after it, which is documented, so kinds are never compared with each other."""
    bad = monotonic_violations(((t, k), us) for t, k, us, _, _ in frames)
    streams = len({(t, k) for t, k, _, _, _ in frames})
    checks = [("soak_monotonic", not bad,
               "; ".join(f"0x{t:02x} {kind_name(k)}: {v['count']} backward (worst {v['worst']} us)"
                         for (t, k), v in sorted(bad.items())) or f"{len(frames)} frames in {streams} type/kind streams never went backward")]
    order = time_sync_order_violations(sync_rows)
    checks.append(("soak_time_sync_order", not any(order.values()),
                   f"{len(sync_rows)} responses: " + ", ".join(f"{k.replace('_', ' ')} {v}" for k, v in order.items())))
    boots = sorted({b for _, _, _, b, _ in frames})
    checks.append(("soak_boot_id", boots == [boot_id],
                   "boot_id " + (", ".join(f"0x{b:08x}" for b in boots) or "none seen") + f" (expected 0x{boot_id:08x})"))
    return checks


def judge_rtt(groups, window_s=RTT_WINDOW_S):
    """groups: name -> effective round trips in seconds, for 'burst', 'single_1s' and 'single_30s'.

    open() sends a burst; the periodic exchanges after it join the fit only if they are within `window_s` of the
    burst's best round trip. If the singles are slower than that, the fit stays on the burst and align() goes
    None 60 s after open()."""
    burst = groups.get("burst") or []
    if not burst:
        return [("rtt_gate", False, "no burst exchange completed")]
    best = min(burst)
    checks = []
    for name in ("single_1s", "single_30s"):
        singles = groups.get(name) or []
        if not singles:
            checks.append((f"rtt_gate_{name}", False, "no exchange completed"))
            continue
        inside = [r for r in singles if r <= best + window_s]
        checks.append((f"rtt_gate_{name}", len(inside) >= math.ceil(0.95 * len(singles)),
                       f"{len(inside)}/{len(singles)} within burst best {best * 1e6:.0f} us + {window_s * 1e3:g} ms "
                       f"(slowest {max(singles) * 1e6:.0f} us)"))
    return checks


# --- Collection (needs the board) -------------------------------------------------------------------

def receive_until(s, deadline):
    """Like receive(), but returns at the monotonic `deadline` however long the bus is quiet, so a caller can
    keep its own send schedule. Leaves the socket timeout changed: the caller restores it."""
    while True:
        wait = deadline - time.monotonic()
        if wait <= 0:
            return
        s.settimeout(max(wait, 0.0005))
        try:
            raw, ancillary, flags, _ = s.recvmsg(FRAME.size, 256)
        except socket.timeout:
            return
        message = decode_stamped(raw, ancillary, flags)
        if message is not None:
            yield message


def exchange_rows(sent, answers, boot_id):
    """Pair each valid TIME_SYNC response with the echo of its request. A round trip needs both ends stamped by
    the same clock: the adapter's hardware clock if both have a hardware stamp, else the kernel's software clock.
    The hardware clock is used when it covers at least as many exchanges. Returns (rows, source, counts)."""
    valid = []
    for rid in sorted(answers):
        rx_us, q_us, t, rx_sw, rx_hw = answers[rid]
        if (rid not in sent or t["boot_id"] != boot_id or t["kind"] != TX_QUEUE or t["board_us"] != q_us
                or rx_us > q_us):
            continue
        valid.append((sent[rid][0], sent[rid][1], rx_sw, rx_hw, rx_us, q_us))
    hardware = [(v[1], v[3], v[4], v[5]) for v in valid if v[1] and v[3]]
    software = [(v[0], v[2], v[4], v[5]) for v in valid if v[0] and v[2]]
    chosen, source = (hardware, "hardware") if len(hardware) >= len(software) else (software, "software")
    rows = [((tx + rx) / 2e9, (rx_us + q_us) / 2e6, (rx - tx) / 1e9, (rx - tx) / 1e9 - (q_us - rx_us) / 1e6)
            for tx, rx, rx_us, q_us in chosen]
    return rows, source, {"valid": len(valid), "hardware": len(hardware), "software": len(software)}


def run_exchange_phase(s, address, boot_id, label, id_base, seconds, rate, frames):
    """TIME_SYNC requests at `rate` per second for `seconds`, receiving everything the board sends meanwhile.
    Appends every trailer-bearing board frame to `frames`."""
    period = 1.0 / rate
    sent, answers, requests, send_errors = {}, {}, 0, 0
    start = time.monotonic()
    end = start + seconds
    next_send, next_progress = start, start + 30.0
    print(f"  [{label}] {seconds:.0f} s at {rate:g} requests/s", flush=True)
    try:
        while True:
            now = time.monotonic()
            if now >= end:
                break
            if now >= next_send:
                requests += 1
                try:
                    send(s, TIME_SYNC_REQUEST, address, struct.pack("<IQ", id_base + requests, time.perf_counter_ns()))
                except OSError:
                    send_errors += 1
                next_send += period
                if next_send < now:  # the host stalled: do not burst to catch up
                    next_send = now + period
            if now >= next_progress:
                print(f"  [{label}] {now - start:.0f}/{seconds:.0f} s, {requests} sent, {len(answers)} answered", flush=True)
                next_progress += 30.0
            for can_id, data, own, _, _, sw, hw in receive_until(s, min(next_send, end)):
                msg_type, sender = can_id >> 5, can_id & 0x1F
                if own:
                    if can_id == (TIME_SYNC_REQUEST << 5) | address:
                        rid = struct.unpack_from("<I", data)[0]
                        if id_base < rid <= id_base + requests:
                            sent[rid] = (sw, hw)
                    continue
                if sender != address:
                    continue
                t = trailer(msg_type, data)
                if t is None:
                    continue
                frames.append((msg_type, t["kind"], t["board_us"], t["boot_id"], t["sequence"]))
                if msg_type == TIME_SYNC_RESPONSE:
                    rid, rx_us, q_us = struct.unpack_from("<IQQ", data)
                    if id_base < rid <= id_base + requests:
                        answers[rid] = (rx_us, q_us, t, sw, hw)
    finally:
        s.settimeout(0.05)
    rows, source, counts = exchange_rows(sent, answers, boot_id)
    result = analyze_exchanges(rows)
    result.update({"requests": requests, "answered": len(answers), "echoed": len(sent), "send_errors": send_errors,
                   "source": source, "counts": counts})
    sync_rows = [(rid, a[0], a[1]) for rid, a in answers.items()]
    return result, sync_rows


def run_soak(s, address, boot_id, args):
    frames, sync_rows, phases = [], [], {}
    print(f"clock soak: {args.soak_seconds:.0f} s idle at {args.soak_idle_rate:g} request/s, then "
          f"{args.soak_seconds:.0f} s under load at {args.soak_load_rate:g} requests/s", flush=True)
    for label, id_base, rate in (("idle", 0x7A100000, args.soak_idle_rate), ("load", 0x7A200000, args.soak_load_rate)):
        phases[label], rows = run_exchange_phase(s, address, boot_id, label, id_base, args.soak_seconds, rate, frames)
        sync_rows += rows
        a = phases[label]
        print(f"  {_describe_phase(label, a)}", flush=True)
        print(f"  [{label}] {a['source']} stamps on {max(a['counts']['hardware'], a['counts']['software'])} of "
              f"{a['counts']['valid']} valid exchanges (hardware {a['counts']['hardware']}, software {a['counts']['software']}); "
              f"effective round trip {_fmt_us(a.get('effective_rtt_us'))}", flush=True)
        if label == "idle":
            time.sleep(2.0)
    gaps = sum(1 for a, b in zip(frames, frames[1:]) if b[3] == a[3] and b[4] != a[4] + 1)
    print(f"  board frames with a trailer: {len(frames)}; sequence gaps seen by this host: {gaps} (information only, "
          "the host socket can drop frames too)", flush=True)
    checks = judge_soak(phases["idle"], phases["load"]) + judge_ordering(frames, sync_rows, boot_id)
    for name, ok, detail in checks:
        report(name, ok, detail)
    return {"phases": phases, "frames": len(frames), "sequence_gaps": gaps}


def timed_exchange(s, address, boot_id, request_id, timeout=0.25):
    """One TIME_SYNC exchange, sent now. Returns (host_rtt_s, effective_rtt_s, "hardware" or "software") or None."""
    send(s, TIME_SYNC_REQUEST, address, struct.pack("<IQ", request_id, time.perf_counter_ns()))
    tx = answer = None
    for can_id, data, own, _, _, sw, hw in receive_until(s, time.monotonic() + timeout):
        if own and can_id == (TIME_SYNC_REQUEST << 5) | address:
            if struct.unpack_from("<I", data)[0] == request_id:
                tx = (sw, hw)
        elif not own and can_id == (TIME_SYNC_RESPONSE << 5) | address:
            rid, rx_us, q_us = struct.unpack_from("<IQQ", data)
            t = trailer(TIME_SYNC_RESPONSE, data)
            if rid == request_id and t is not None and t["boot_id"] == boot_id and t["board_us"] == q_us and rx_us <= q_us:
                answer = (rx_us, q_us, sw, hw)
        if tx is not None and answer is not None:
            break
    if tx is None or answer is None:
        return None
    if tx[1] and answer[3]:
        start, end, source = tx[1], answer[3], "hardware"
    elif tx[0] and answer[2]:
        start, end, source = tx[0], answer[2], "software"
    else:
        return None
    host = (end - start) / 1e9
    return host, host - (answer[1] - answer[0]) / 1e6, source


def run_rtt_report(s, address, boot_id, args):
    groups = {"burst": [], "single_1s": [], "single_30s": []}
    host = {name: [] for name in groups}
    lost = {name: 0 for name in groups}
    sources = set()
    request_id = [0x7A300000]

    def one(name):
        request_id[0] += 1
        try:
            result = timed_exchange(s, address, boot_id, request_id[0])
        finally:
            s.settimeout(0.05)
        if result is None:
            lost[name] += 1
            return
        host[name].append(result[0])
        groups[name].append(result[1])
        sources.add(result[2])

    print(f"round-trip report: {args.rtt_bursts} bursts of 4, {args.rtt_singles_1s} singles 1 s apart, "
          f"{args.rtt_singles_30s} singles 30 s apart", flush=True)
    for i in range(args.rtt_bursts):
        for _ in range(4):  # as reachAQ's open() does: each request goes out when the previous reply is in
            one("burst")
        time.sleep(1.0)
    for name, count, spacing in (("single_1s", args.rtt_singles_1s, 1.0), ("single_30s", args.rtt_singles_30s, 30.0)):
        due = time.monotonic() + (spacing if spacing > 1.0 else 0.0)
        for i in range(count):
            time.sleep(max(0.0, due - time.monotonic()))
            one(name)
            due += spacing
            if spacing > 1.0:
                print(f"  [{name}] {i + 1}/{count}", flush=True)
    stats = {name: {"effective_us": _micro(summarize(groups[name])), "host_us": _micro(summarize(host[name])),
                    "lost": lost[name]} for name in groups}
    for name in groups:
        e, h = stats[name]["effective_us"], stats[name]["host_us"]
        if e is None:
            print(f"  {name}: no exchange completed ({lost[name]} lost)")
            continue
        print(f"  {name}: n={e['n']} lost={lost[name]}  effective round trip us min {e['min']:.0f} median {e['median']:.0f} "
              f"p95 {e['p95']:.0f} max {e['max']:.0f}   (host round trip us min {h['min']:.0f} median {h['median']:.0f} "
              f"p95 {h['p95']:.0f} max {h['max']:.0f})", flush=True)
    print(f"  stamps: {' and '.join(sorted(sources)) or 'none'}; "
          "effective = host round trip minus the board's own handling time, the figure reachAQ's fit uses")
    for name, ok, detail in judge_rtt(groups):
        report(name, ok, detail)
    return stats


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--channel", default="can0")
    parser.add_argument("--address", type=int, default=0)
    parser.add_argument("--clapper-address", type=int, default=8,
                        help="address the fake SYNC_CLAP frames are sent from (default 8, an IO-module address)")
    parser.add_argument("--play-tone", action="store_true")
    parser.add_argument("--soak", action="store_true",
                        help="board clock against the host, idle then under TIME_SYNC load (replaces the standard checks)")
    parser.add_argument("--soak-seconds", type=float, default=335.0,
                        help="length of each soak phase; at least 330 s (13 wraps) is needed to pass (default 335)")
    parser.add_argument("--soak-idle-rate", type=float, default=1.0, help="TIME_SYNC requests per second when idle")
    parser.add_argument("--soak-load-rate", type=float, default=200.0, help="TIME_SYNC requests per second under load")
    parser.add_argument("--rtt-report", action="store_true",
                        help="TIME_SYNC round trips: bursts of 4 and singles 1 s and 30 s apart (replaces the standard checks)")
    parser.add_argument("--rtt-bursts", type=int, default=5)
    parser.add_argument("--rtt-singles-1s", type=int, default=45)
    parser.add_argument("--rtt-singles-30s", type=int, default=6)
    parser.add_argument("--json", metavar="PATH", help="also write every result to this file as JSON")
    args = parser.parse_args()
    if not 0 <= args.clapper_address <= 31 or args.clapper_address == args.address:
        parser.error("--clapper-address must be 0-31 and differ from --address")
    if (args.soak or args.rtt_report) and args.play_tone:
        parser.error("--soak and --rtt-report run instead of the standard checks, so --play-tone has no effect with them")
    if args.soak_seconds <= 0 or args.soak_idle_rate <= 0 or args.soak_load_rate <= 0:
        parser.error("--soak-seconds and the soak rates must be positive")

    s = open_socket(args.channel)
    boot_id = check_capabilities(s, args.address)
    extra = {}
    if boot_id is not None:
        if args.soak or args.rtt_report:
            if args.soak:
                extra["soak"] = run_soak(s, args.address, boot_id, args)
            if args.rtt_report:
                extra["rtt"] = run_rtt_report(s, args.address, boot_id, args)
        else:
            check_time_sync(s, args.address, boot_id)
            check_claps(s, args.address, boot_id, args.clapper_address)
            check_periodic_trailers(s, args.address, boot_id)
            if args.play_tone:
                check_tone(s, args.address)
    if args.json:
        with open(args.json, "w") as out:
            json.dump({"boot_id": boot_id, "checks": report_log, **extra}, out, indent=1, default=str)
    return 0 if results and all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
