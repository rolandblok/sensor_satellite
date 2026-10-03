"""Plot a flash-log dump: Vcap per wake, and the net current across each sleep.

    python tools/plot_log.py data/2026-10-03/flashlog_162922.csv

The log has no clock. Time is rebuilt from slept_s plus the awake time each
kind of wake costs (~9 s for a full cycle, ~0.3 s for a held poll), counted
from the last cold boot that starts an unbroken run (--from-seq, default: the
first record). Records after a gap in bootN (an RTC loss) are left off.
Writes <csv>.png beside the CSV.
"""
import argparse, csv
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

AWAKE_FULL_S, AWAKE_HELD_S = 9.1, 0.3
INK, INK2, GRID, BLUE, SURF = "#0b0b0b", "#52514e", "#e4e3df", "#2a78d6", "#fcfcfb"

ap = argparse.ArgumentParser()
ap.add_argument("csv")
ap.add_argument("--from-seq", type=int, default=1)
a = ap.parse_args()

rows = [r for r in csv.DictReader(open(a.csv)) if int(r["seq"]) >= a.from_seq]
t, run = 0.0, []
for i, r in enumerate(rows):
    if i and int(r["bootN"]) != int(rows[i - 1]["bootN"]) + 1:
        break                                   # RTC lost - the clock is gone
    if i:
        t += int(r["slept_s"]) + (AWAKE_HELD_S if rows[i - 1]["held"] == "1" else AWAKE_FULL_S)
    run.append((t / 3600, r))

h = [x for x, _ in run]
vb = [int(r["v_boot_mV"]) / 1000 for _, r in run]
ua = [float(r["net_uA"]) for _, r in run]
held0 = next((x for x, r in run if r["held"] == "1"), None)

plt.rcParams.update({"font.size": 9, "axes.edgecolor": GRID, "axes.labelcolor": INK2,
                     "xtick.color": INK2, "ytick.color": INK2, "text.color": INK})
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(9, 6.2), sharex=True, facecolor=SURF,
                               gridspec_kw={"height_ratios": [3, 2], "hspace": 0.12})
for ax in (ax1, ax2):
    ax.set_facecolor(SURF)
    ax.grid(axis="y", color=GRID, lw=0.8)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    if held0 is not None:
        ax.axvspan(held0, h[-1] + 0.25, color="#f0efec", lw=0, zorder=0)

ax1.plot(h, vb, color=BLUE, lw=2, marker="o", ms=3.5)
for v, lab in ((3.80, "resume 3.80"), (3.50, "hold 3.50"), (3.25, "floor 3.25")):
    ax1.axhline(v, color=INK2, lw=0.8, ls=(0, (4, 3)))
    ax1.text(h[0], v + 0.02, lab, color=INK2, fontsize=8)
ax1.set_ylabel("Vcap at wake (V)")
ax1.set_title("Vcap per wake, one unbroken run (no clock - time rebuilt from sleep lengths)",
              loc="left", fontsize=10)
if held0 is not None:
    ax1.text(held0 + 0.05, max(vb) - 0.05, "held off: 900 s polls,\nno refresh",
             color=INK2, fontsize=8, va="top")
    ax1.annotate("last record; next poll never logged", (h[-1], vb[-1]),
                 (h[-1] - 3.2, vb[-1] - 0.05), fontsize=8, color=INK2,
                 arrowprops={"arrowstyle": "-", "color": INK2, "lw": 0.8})

ax2.bar(h, ua, width=0.07, color=BLUE)
ax2.axhline(0, color=INK2, lw=0.8)
ax2.set_ylabel("net current in sleep (uA)\nharvest minus sleep draw")
ax2.set_xlabel("hours since the run's cold boot")
fig.savefig(a.csv[:-4] + ".png", dpi=150, bbox_inches="tight")
print("wrote", a.csv[:-4] + ".png", f"- {len(run)} records over {h[-1]:.1f} h")
