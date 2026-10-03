"""Turn a tools/scope_log.py CSV into the number this investigation is after.

Wake and sleep of the whole sculpture, from two channels across one shunt
(CH1 coarse for the wake, CH2 fine for the sleep):
    python sleep_current/analyze.py cycle data/<date>/S_cycle.csv --ohms 10.1 \
        --wake-offset-mv <CH1 zero> --offset-mv <CH2 zero>

Sleep current, from a low-side shunt on one channel:
    python sleep_current/analyze.py sleep data/2026-10-04/A_5V_4.0V.csv --ohms 100

Cap self-discharge, from the cap voltage on one channel:
    python sleep_current/analyze.py leak data/2026-10-04/cap_leak.csv --farads 4

sleep: current is V/ohms per sample. A sample whose window caught a wake burst
reads far above the sleep plateau, so anything above 3x the median is dropped
as a wake and counted. Prints the median of what is left: that is the figure.

leak:  least-squares slope of V against time, then I = C dV/dt. --skip-h drops
the first hours, where charge redistribution inside the cap, not leakage,
dominates. The same fit is also printed per hour, so you can see it settle.

cycle: a wake is where the CH1 current exceeds --wake-ma. Each wake's charge is
the sum of I dt over it; sleep is the CH2 median outside the wakes and
--settle-s after each. Both are also stated as millivolts on a --farads pack,
the units the flash log uses.

--offset-mv subtracts the probe's zero, read with a wire across the shunt. On
10 ohm it is worth 100 uA per mV - often more than the thing being measured.
"""
import argparse, csv, statistics, sys


def rows_of(path):
    with open(path) as f:
        rows = [r for r in csv.reader(f) if r and not r[0].startswith("#")]
    return rows[0], rows[1:]


def num(x):
    return float(x) if x.strip() else None   # blank = channel off screen


def load(path, col):
    head, body = rows_of(path)
    i = head.index(col) if col else 1        # first channel column by default
    t, v = [], []
    for r in body:
        if i < len(r) and num(r[i]) is not None:
            t.append(float(r[0]))
            v.append(num(r[i]))
    return t, v, head[i]


def cycle(a):
    head, body = rows_of(a.csv)
    iw, isl = head.index(a.wake_col), head.index(a.sleep_col)
    t  = [float(r[0]) for r in body]
    vw = [num(r[iw]) for r in body]
    vs = [num(r[isl]) for r in body]
    ma = [None if v is None else (v * 1000 - a.wake_offset_mv) / a.ohms for v in vw]

    # wakes: runs of samples above the threshold, tolerating one-sample gaps
    wakes, cur = [], None
    for k, x in enumerate(ma):
        hot = x is not None and x > a.wake_ma
        if hot:
            if cur and t[k] - t[cur[1]] <= 2.0:
                cur[1] = k
            else:
                cur = [k, k]
                wakes.append(cur)
    if not wakes:
        sys.exit(f"no wake above {a.wake_ma} mA on {a.wake_col}")

    print(f"{a.csv}  ({a.ohms:g} ohm, zeros {a.wake_offset_mv:g} / {a.offset_mv:g} mV)")
    print("  wake  start_s  dur_s  mean_mA  max_mA   Q_mC  = mV on %g F" % a.farads)
    qs, durs = [], []
    for n, (i0, i1) in enumerate(wakes):
        q = 0.0
        for k in range(i0, i1 + 1):
            dt = (t[k + 1] - t[k]) if k + 1 < len(t) else (t[k] - t[k - 1])
            q += max(ma[k] or 0.0, 0.0) * min(dt, 2.0)     # mA x s = mC
        dur = t[i1] - t[i0] + (t[i1] - t[i1 - 1] if i1 > i0 else 0.25)
        hot = [ma[k] for k in range(i0, i1 + 1) if ma[k] is not None]
        qs.append(q); durs.append(dur)
        print(f"  {n:4d} {t[i0]:8.1f} {dur:6.1f} {q / dur:8.1f} {max(hot):7.1f} {q:6.0f}"
              f"  = {q / a.farads:5.1f}")
    starts = [t[i0] for i0, _ in wakes]
    period = statistics.median([b - c for b, c in zip(starts[1:], starts)]) if len(starts) > 1 else None

    def near_wake(x):
        return any(t[i0] - 1.0 <= x <= t[i1] + a.settle_s for i0, i1 in wakes)
    sl = [(v * 1000 - a.offset_mv) / a.ohms * 1000
          for x, v in zip(t, vs) if v is not None and not near_wake(x)]
    if not sl:
        sys.exit("no sleep samples outside the wakes")
    i_sl = statistics.median(sl)
    print(f"  sleep: median {i_sl:.1f} uA over {len(sl)} samples"
          f" (min {min(sl):.1f}, max {max(sl):.1f})")
    print(f"         = {i_sl * 300 / a.farads / 1000:.1f} mV per 300 s,"
          f" {i_sl * 900 / a.farads / 1000:.1f} mV per 900 s on {a.farads:g} F")
    if period:
        q = statistics.median(qs)
        avg = (q + i_sl / 1000 * (period - statistics.median(durs))) / period
        print(f"  cycle: {period:.1f} s, median wake {q:.0f} mC; average {avg:.2f} mA"
              f" = {avg * period / a.farads:.0f} mV per cycle without harvest")


def slope(t, v):
    n = len(t)
    mt, mv = sum(t) / n, sum(v) / n
    num = sum((a - mt) * (b - mv) for a, b in zip(t, v))
    den = sum((a - mt) ** 2 for a in t)
    return num / den


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["sleep", "leak", "cycle"])
    ap.add_argument("csv")
    ap.add_argument("--col", help="column name, default the first channel")
    ap.add_argument("--ohms", type=float, default=100.0)
    ap.add_argument("--farads", type=float, default=4.0)
    ap.add_argument("--offset-mv", type=float, default=0.0)
    ap.add_argument("--skip-h", type=float, default=0.0)
    ap.add_argument("--wake-col", default="ch1_V")
    ap.add_argument("--sleep-col", default="ch2_V")
    ap.add_argument("--wake-offset-mv", type=float, default=0.0)
    ap.add_argument("--wake-ma", type=float, default=2.0)
    ap.add_argument("--settle-s", type=float, default=8.0)
    a = ap.parse_args()

    if a.mode == "cycle":
        return cycle(a)

    t, v, name = load(a.csv, a.col)
    v = [x - a.offset_mv / 1000 for x in v]

    if a.mode == "sleep":
        ua = [x / a.ohms * 1e6 for x in v]
        med = statistics.median(ua)
        keep = [x for x in ua if x <= 3 * med]
        if not keep:
            sys.exit("no samples left after dropping wakes")
        print(f"{a.csv}  ({name}, {a.ohms:g} ohm, offset {a.offset_mv:g} mV)")
        print(f"  samples {len(ua)}, dropped as wakes {len(ua) - len(keep)}")
        print(f"  sleep current  median {statistics.median(keep):7.1f} uA")
        print(f"                 mean   {statistics.mean(keep):7.1f} uA")
        print(f"                 min    {min(keep):7.1f} uA   max {max(keep):7.1f} uA")
        return

    pts = [(x, y) for x, y in zip(t, v) if x >= a.skip_h * 3600]
    if len(pts) < 3:
        sys.exit("not enough samples after --skip-h")
    tt, vv = zip(*pts)
    s = slope(tt, vv)
    print(f"{a.csv}  ({name}, {a.farads:g} F, from {a.skip_h:g} h)")
    print(f"  V {vv[0]:.4f} -> {vv[-1]:.4f} over {(tt[-1] - tt[0]) / 3600:.2f} h")
    print(f"  dV/dt {s * 3600 * 1000:+.2f} mV/h  ->  leakage {-s * a.farads * 1e6:.1f} uA")
    print("  per hour:")
    h = 0
    while True:
        seg = [(x, y) for x, y in pts if h * 3600 <= x - tt[0] < (h + 1) * 3600]
        if len(seg) < 3:
            break
        st, sv = zip(*seg)
        print(f"    h{h:<3} {-slope(st, sv) * a.farads * 1e6:7.1f} uA at {sv[0]:.3f} V")
        h += 1


if __name__ == "__main__":
    main()
