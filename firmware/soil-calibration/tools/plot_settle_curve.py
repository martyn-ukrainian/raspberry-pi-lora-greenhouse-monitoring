#!/usr/bin/env python3
"""Графік R(t): як показник повертається до R15 після доливу.

Малює експоненту R(t) = R15 - ΔR*exp(-t/τ) за заміряними Rmin/R15/τ (див.
"Метрика R15" у docs/калібрування-ґрунту.md) — не ілюстративні дані, а
підставлені реальні числа з `--rmin/--r15/--tau`.

    uv run --with matplotlib tools/plot_settle_curve.py \
        --rmin 916 --r15 1018 --tau 2.5 --window 15 --out ../data/settle_curve.png
"""

import argparse
import math
import sys
from pathlib import Path

try:
    import matplotlib.pyplot as plt
except ImportError:
    sys.exit("Потрібен matplotlib: uv run --with matplotlib tools/plot_settle_curve.py ...")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--rmin", type=float, required=True, help="показник одразу після доливу (провал)")
    parser.add_argument("--r15", type=float, required=True, help="усталений показник на 15-й хвилині")
    parser.add_argument("--tau", type=float, required=True, help="стала часу, хвилини")
    parser.add_argument("--window", type=float, default=15.0, help="скільки хвилин малювати (типово 15)")
    parser.add_argument("--out", type=Path, default=Path(__file__).resolve().parent.parent / "data" / "settle_curve.png")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    delta_r = args.r15 - args.rmin

    n = 200
    ts = [args.window * i / (n - 1) for i in range(n)]
    rs = [args.r15 - delta_r * math.exp(-t / args.tau) for t in ts]

    fig, ax = plt.subplots(figsize=(8, 5), dpi=150)
    ax.plot(ts, rs, color="#2a7f62", linewidth=2, label="R(t)")
    ax.axhline(args.r15, color="#888888", linestyle="--", linewidth=1, label=f"R15 = {args.r15:.0f} мВ")
    ax.scatter([0], [args.rmin], color="#d17a1f", zorder=5, label=f"Rmin = {args.rmin:.0f} мВ (t=0)")

    for k in (1, 2, 3):
        t_k = k * args.tau
        if t_k <= args.window:
            r_k = args.r15 - delta_r * math.exp(-k)
            ax.axvline(t_k, color="#cccccc", linestyle=":", linewidth=1)
            ax.annotate(f"{k}τ", xy=(t_k, r_k), xytext=(4, -12), textcoords="offset points",
                        fontsize=8, color="#777777")

    ax.set_xlabel("Хвилин після доливу (t)")
    ax.set_ylabel("Показник сенсора (мВ)")
    ax.set_title(f"R(t) = R15 − ΔR·e^(−t/τ)   (ΔR={delta_r:.0f} мВ, τ={args.tau:.1f} хв)", fontsize=11)
    ax.grid(True, linestyle="--", alpha=0.4)
    ax.legend(loc="lower right", fontsize=8)
    fig.tight_layout()

    args.out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.out)
    print(f"збережено: {args.out}")


if __name__ == "__main__":
    main()
