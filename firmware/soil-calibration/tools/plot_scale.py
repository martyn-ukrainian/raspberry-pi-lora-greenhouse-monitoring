#!/usr/bin/env python3
"""Графік шкали: сире ADC -> відсоток, лінійно проти логарифмічно.

Показує те, заради чого шкалу й міняли: лінійне відображення з'їдає мокру
половину діапазону, у якій теплиця і живе.

    uv run --with matplotlib tools/plot_scale.py
"""

import math
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

DRY, WET, ASY = 2874, 1050, 1043
OUT = Path(__file__).resolve().parent.parent / "data" / "scale_curve.png"

# Заміряні точки (docs/дослідження/калібрування-ґрунту.md)
MARKS = [
    (2874, "сухий ґрунт\nстіл 22.08"),
    (1722, "відро\n+50 мл"),
    (1189, "відро\nточка 1"),
    (1050, "ГРЯДКА\nпісля поливу"),
]


def log_pct(r: float) -> float:
    if r <= ASY + 1:
        return 100.0
    if r >= DRY:
        return 0.0
    span = DRY - ASY
    return 100.0 * math.log((r - ASY) / span) / math.log((WET - ASY) / span)


def lin_pct(r: float) -> float:
    return max(0.0, min(100.0, 100.0 * (DRY - r) / (DRY - WET)))


def main() -> None:
    xs = [DRY - i * (DRY - WET) / 400 for i in range(401)]
    lin = [lin_pct(x) for x in xs]
    log = [log_pct(x) for x in xs]

    fig, (ax, ax2) = plt.subplots(
        2, 1, figsize=(11, 9), height_ratios=[3, 2], sharex=True
    )
    fig.patch.set_facecolor("#faf9f7")
    for a in (ax, ax2):
        a.set_facecolor("#faf9f7")
        a.invert_xaxis()          # сухо ліворуч, мокро праворуч
        a.grid(alpha=0.25, linestyle=":")
        for s in ("top", "right"):
            a.spines[s].set_visible(False)

    ax.plot(xs, lin, lw=2.2, color="#b0653a", label="лінійно — map()")
    ax.plot(xs, log, lw=3.0, color="#2f6b4f", label="логарифмічно — за кривою")

    # Центр шкали: 50% на логарифмічній
    mid = min(xs, key=lambda x: abs(log_pct(x) - 50))
    ax.axhline(50, color="#888", lw=1, ls="--", alpha=0.7)
    ax.plot([mid], [50], "o", ms=11, color="#2f6b4f", zorder=5)
    ax.annotate(
        f"центр шкали\n{mid:.0f} raw = 50%",
        xy=(mid, 50), xytext=(mid + 330, 34),
        fontsize=10, color="#2f6b4f",
        arrowprops=dict(arrowstyle="->", color="#2f6b4f", lw=1.4),
    )
    ax.annotate(
        f"лінійно те саме місце\nдає {lin_pct(mid):.0f}%",
        xy=(mid, lin_pct(mid)), xytext=(mid + 300, lin_pct(mid) + 12),
        fontsize=9.5, color="#b0653a",
        arrowprops=dict(arrowstyle="->", color="#b0653a", lw=1.2),
    )

    for raw, label in MARKS:
        ax.axvline(raw, color="#bbb", lw=0.9, ls=":", alpha=0.8)
        ax.text(raw, 103, label, fontsize=8.5, ha="center",
                va="bottom", color="#555", linespacing=1.3)

    ax.set_ylabel("вологість, %")
    ax.set_ylim(-3, 118)
    ax.legend(loc="center left", framealpha=0.95, fontsize=10)
    ax.set_title(
        "Шкала вологості: чому логарифм, а не пряма",
        fontsize=13, pad=42, weight="bold",
    )

    # Нижня панель: скільки відсотків дає один крок raw
    d_lin = [abs(lin_pct(x + 5) - lin_pct(x - 5)) / 10 for x in xs[1:-1]]
    d_log = [abs(log_pct(x + 5) - log_pct(x - 5)) / 10 for x in xs[1:-1]]
    ax2.plot(xs[1:-1], d_lin, lw=2.0, color="#b0653a")
    ax2.plot(xs[1:-1], d_log, lw=2.8, color="#2f6b4f")
    ax2.set_ylabel("% на одиницю raw")
    ax2.set_xlabel("сире ADC   (ліворуч сухо  →  праворуч мокро)")
    ax2.set_title(
        "Крутизна шкали: до мокрого відсоток біжить швидше, до сухого — повільніше",
        fontsize=10.5, pad=8,
    )
    ax2.axvline(mid, color="#888", lw=1, ls="--", alpha=0.7)

    fig.tight_layout()
    fig.savefig(OUT, dpi=145, facecolor=fig.get_facecolor())
    print(f"збережено: {OUT}")
    print(f"центр шкали: {mid:.0f} raw = 50% (лінійно там {lin_pct(mid):.0f}%)")


if __name__ == "__main__":
    main()
