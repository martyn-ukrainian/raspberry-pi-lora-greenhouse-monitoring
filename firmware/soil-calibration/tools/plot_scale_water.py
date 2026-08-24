#!/usr/bin/env python3
"""Шкала проти ВОДИ: чому логарифм природний, а пряма — ні.

Той самий факт, поданий інакше, ніж у plot_scale.py. Там віссю було сире ADC;
тут — влита вода, тобто те, що нас насправді цікавить.

На цій осі логарифмічна шкала стає ПРЯМОЮ (за побудовою: рівні порції води =
рівні кроки відсотка), а лінійний map() вигинається. Це показує, котра з двох
шкал відповідає фізиці, а котра — зручності програміста.

    uv run --with matplotlib python tools/plot_scale_water.py
"""

import math
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

# Модель, заміряна на відрі 1 кг: mV = C + A*exp(-V/k)
C_MV, DRY_MV, K_ML = 874.0, 2409.0, 52.0
A_MV = DRY_MV - C_MV
MV_PER_RAW = 2409.0 / 2874.0

DRY_RAW, WET_RAW, ASY_RAW = 2874, 1050, 1043
OUT = Path(__file__).resolve().parent.parent / "data" / "scale_vs_water.png"


def mv_at(water_ml: float) -> float:
    return C_MV + A_MV * math.exp(-water_ml / K_ML)


def raw_at(water_ml: float) -> float:
    return mv_at(water_ml) / MV_PER_RAW


def log_pct(r: float) -> float:
    if r <= ASY_RAW + 1:
        return 100.0
    if r >= DRY_RAW:
        return 0.0
    span = DRY_RAW - ASY_RAW
    return 100.0 * math.log((r - ASY_RAW) / span) / math.log((WET_RAW - ASY_RAW) / span)


def lin_pct(r: float) -> float:
    return max(0.0, min(100.0, 100.0 * (DRY_RAW - r) / (DRY_RAW - WET_RAW)))


def main() -> None:
    # До скількох мілілітрів іде шкала: доки не дійдемо до мокрого кінця
    v_max = -K_ML * math.log((WET_RAW * MV_PER_RAW - C_MV) / A_MV)
    xs = [v_max * i / 300 for i in range(301)]

    fig, (ax, ax2) = plt.subplots(1, 2, figsize=(13.5, 5.8), width_ratios=[3, 2])
    fig.patch.set_facecolor("#faf9f7")
    for a in (ax, ax2):
        a.set_facecolor("#faf9f7")
        a.grid(alpha=0.25, linestyle=":")
        for s in ("top", "right"):
            a.spines[s].set_visible(False)

    ax.plot(xs, [lin_pct(raw_at(v)) for v in xs], lw=2.2, color="#b0653a",
            label="лінійно — map()")
    ax.plot(xs, [log_pct(raw_at(v)) for v in xs], lw=3.0, color="#2f6b4f",
            label="логарифмічно — за кривою")

    mid_v = v_max / 2
    ax.axvline(mid_v, color="#888", lw=1, ls="--", alpha=0.7)
    ax.plot([mid_v], [log_pct(raw_at(mid_v))], "o", ms=11, color="#2f6b4f", zorder=5)
    ax.plot([mid_v], [lin_pct(raw_at(mid_v))], "o", ms=9, color="#b0653a", zorder=5)
    ax.annotate(
        f"половина води\nлогарифм: {log_pct(raw_at(mid_v)):.0f}%",
        xy=(mid_v, log_pct(raw_at(mid_v))), xytext=(mid_v + 14, 34),
        fontsize=10, color="#2f6b4f",
        arrowprops=dict(arrowstyle="->", color="#2f6b4f", lw=1.4),
    )
    ax.annotate(
        f"пряма каже {lin_pct(raw_at(mid_v)):.0f}%",
        xy=(mid_v, lin_pct(raw_at(mid_v))), xytext=(mid_v + 12, 88),
        fontsize=10, color="#b0653a",
        arrowprops=dict(arrowstyle="->", color="#b0653a", lw=1.2),
    )

    ax.set_xlabel("влита вода, мл   (відро 1 кг)")
    ax.set_ylabel("вологість, %")
    ax.set_ylim(-3, 105)
    ax.legend(loc="lower right", framealpha=0.95, fontsize=10)
    ax.set_title(
        "На осі води логарифм — ПРЯМА, а map() вигинається",
        fontsize=12.5, weight="bold", pad=10,
    )

    # Стовпчики: скільки відсотків дають однакові порції по 25 мл
    step = 25.0
    n = int(v_max // step)
    centers = [step * (i + 0.5) for i in range(n)]
    def gain(fn, i):
        return fn(raw_at(step * (i + 1))) - fn(raw_at(step * i))

    d_lin = [gain(lin_pct, i) for i in range(n)]
    d_log = [gain(log_pct, i) for i in range(n)]

    w = step * 0.38
    ax2.bar([c - w / 2 for c in centers], d_lin, width=w, color="#b0653a",
            label="лінійно")
    ax2.bar([c + w / 2 for c in centers], d_log, width=w, color="#2f6b4f",
            label="логарифмічно")
    ax2.set_xlabel("влита вода, мл")
    ax2.set_ylabel("приріст відсотка за 25 мл")
    ax2.legend(framealpha=0.95, fontsize=10)
    ax2.set_title(
        "Однакові порції води —\nу логарифма однакові кроки відсотка",
        fontsize=11.5, pad=10,
    )

    fig.tight_layout()
    fig.savefig(OUT, dpi=145, facecolor=fig.get_facecolor())
    print(f"збережено: {OUT}")
    half_log = log_pct(raw_at(mid_v))
    half_lin = lin_pct(raw_at(mid_v))
    print(f"повна шкала = {v_max:.0f} мл")
    print(f"половина води -> логарифм {half_log:.0f}%, пряма {half_lin:.0f}%")
    print(f"перші 25 мл: пряма {d_lin[0]:.1f} п.п., логарифм {d_log[0]:.1f} п.п.")
    print(f"останні 25 мл: пряма {d_lin[-1]:.1f} п.п., логарифм {d_log[-1]:.1f} п.п.")


if __name__ == "__main__":
    main()
