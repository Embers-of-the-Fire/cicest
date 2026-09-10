#!/usr/bin/env python3
"""RQ4 figure: availability checking (lower_fold) as a share of compiler work.

Reads the CSV produced by rq4-compile-phases.sh and writes a horizontal bar
chart of per-phase median times, highlighting the lower_fold phase. The link
stage is deliberately excluded: it is a fixed cost of the prototype's backend
pipeline (~91% of wall-clock total) that no checking discipline affects, so
the figure reports shares of compiler work proper.

Usage: rq4-plot.py <rq4-compile-phases.csv> <output.png>
"""
import csv
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

PHASES = ["parse_modules", "lower_fold", "lir", "codegen"]
LABELS = {
    "parse_modules": "parse + module load",
    "lower_fold": "availability check + fold",
    "lir": "LIR lowering",
    "codegen": "codegen",
}


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2

    csv_path, out_path = sys.argv[1], sys.argv[2]
    rows = list(csv.DictReader(open(csv_path)))

    # Per-phase totals in milliseconds; the figure reports each phase's share
    # of summed compile time (excluding linking) across the e2e suite.
    totals = {phase: 0.0 for phase in PHASES}
    for row in rows:
        for phase in PHASES:
            value = row.get(f"{phase}_ms") or ""
            if value:
                totals[phase] += float(value)
    grand = sum(totals.values())
    shares = [100.0 * totals[phase] / grand for phase in PHASES]

    fig, ax = plt.subplots(figsize=(6.4, 2.2))
    colors = ["#9ecae1" if phase != "lower_fold" else "#de2d26" for phase in PHASES]
    bars = ax.barh([LABELS[p] for p in PHASES], shares, color=colors)
    for bar, share in zip(bars, shares):
        ax.text(
            bar.get_width() + 0.4,
            bar.get_y() + bar.get_height() / 2,
            f"{share:.1f}%",
            va="center",
            fontsize=9,
        )
    ax.set_xlabel("share of compile time, linking excluded (%)")
    ax.set_xlim(0, max(shares) * 1.15)
    ax.invert_yaxis()
    ax.spines[["top", "right"]].set_visible(False)
    fig.tight_layout()
    fig.savefig(out_path, dpi=200)
    print(f"wrote {out_path} (lower_fold share of compiler work: "
          f"{100.0 * totals['lower_fold'] / grand:.2f}%)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
