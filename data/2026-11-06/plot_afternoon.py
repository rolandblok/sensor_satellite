"""Afternoon of 2026-11-06, records 1176-1207 of flashlog_165914.csv, on a clock.

The log has no clock and this stretch has six cold boots, so tools/plot_log.py
(one unbroken run) cannot place it. Times here come from anchors seen live on
the serial console, with timer wakes spread evenly between them (300 s sleeps
measured ~318 s apart wake to wake, 15:06:22 -> 16:57:47 over 21 cycles).

    python data/2026-11-06/plot_afternoon.py
"""
import csv, os
from datetime import datetime, timedelta
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.dates as mdates

HERE = os.path.dirname(os.path.abspath(__file__))
CSV = os.path.join(HERE, "flashlog_165914.csv")
PNG = os.path.join(HERE, "afternoon.png")
INK, INK2, GRID, BLUE, RED, SURF = "#0b0b0b", "#52514e", "#e4e3df", "#2a78d6", "#c8442f", "#fcfcfb"

def T(hms):
    return datetime.strptime("2026-11-06 " + hms, "%Y-%m-%d %H:%M:%S")

CYCLE = timedelta(seconds=318.3)
when = {1176: T("14:34:01"),     # timer wake seen live: hold frame drawn
        1177: T("14:34:17"),     # RESET, poll_s set to 300
        1178: T("14:34:30"),     # cold boot as the console closed (approx.)
        1183: T("15:04:00"),     # port test before the v1.6 flash (approx.)
        1184: T("15:05:58"),     # boot after the v1.6 flash, seen live
        1185: T("15:06:22"),     # RESET, seen live
        1206: T("16:57:47"),     # timer wake, port seen by the v1.7 flasher
        1207: T("16:58:05")}     # boot after the v1.7 flash (approx.)
for n in range(1179, 1183):
    when[n] = when[1178] + (n - 1178) * CYCLE
for n in range(1186, 1206):
    when[n] = when[1185] + (n - 1185) * (when[1206] - when[1185]) / 21

rows = [r for r in csv.DictReader(open(CSV)) if 1176 <= int(r["seq"]) <= 1207]
t = [when[int(r["seq"])] for r in rows]

plt.rcParams.update({"font.size": 9, "axes.edgecolor": GRID, "axes.labelcolor": INK2,
                     "xtick.color": INK2, "ytick.color": INK2, "text.color": INK})
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(9, 6.4), sharex=True, facecolor=SURF,
                               gridspec_kw={"height_ratios": [3, 2], "hspace": 0.12})
for ax in (ax1, ax2):
    ax.set_facecolor(SURF)
    ax.grid(axis="y", color=GRID, lw=0.8)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)

# Sawtooth: each wake drawn from v_boot down (or up) to v_sleep.
for ti, r in zip(t, rows):
    vb, vs = int(r["v_boot_mV"]) / 1000, int(r["v_sleep_mV"]) / 1000
    ax1.plot([ti, ti + timedelta(seconds=40)], [vb, vs], color=RED, lw=1.4)
ax1.plot(t, [int(r["v_boot_mV"]) / 1000 for r in rows], color=BLUE, lw=1.6,
         marker="o", ms=3, label="Vcap at wake")
cold = [(ti, int(r["v_boot_mV"]) / 1000) for ti, r in zip(t, rows) if r["cold"] == "1"]
ax1.scatter([c[0] for c in cold], [c[1] for c in cold], s=55, facecolors="none",
            edgecolors=INK2, lw=1, zorder=5, label="cold boot (reset / flash)")
for v, lab in ((3.80, "resume 3.80"), (3.50, "hold 3.50"), (3.25, "floor 3.25")):
    ax1.axhline(v, color=INK2, lw=0.8, ls=(0, (4, 3)))
    ax1.text(t[0], v + 0.02, lab, color=INK2, fontsize=8)
ax1.set_ylabel("Vcap (V)")
ax1.legend(loc="lower right", frameon=False, fontsize=8)
ax1.set_title("2026-11-06 afternoon, v1.5-v1.7, 4 F cap, cycle 300 s "
              "(red: the drop across each wake)", loc="left", fontsize=10)

dvs = [int(r["dv_sleep_mV"]) for r in rows]
dvc = [int(r["dv_cycle_mV"]) for r in rows]
ax2.bar(t, dvs, width=0.0012, color=BLUE, label="gained across the sleep before")
ax2.bar([ti + timedelta(seconds=60) for ti in t], dvc, width=0.0012, color=RED,
        label="lost across the wake")
ax2.axhline(0, color=INK2, lw=0.8)
ax2.set_ylabel("mV per wake / sleep")
ax2.legend(loc="upper right", frameon=False, fontsize=8)
ax2.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M"))

fig.savefig(PNG, dpi=130, bbox_inches="tight", facecolor=SURF)
print("wrote", PNG)
