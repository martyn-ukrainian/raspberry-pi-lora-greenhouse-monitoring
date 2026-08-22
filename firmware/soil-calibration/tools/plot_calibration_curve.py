#!/usr/bin/env python3
"""Графік калібрувальної кривої одного сенсора: мілілітри -> мілівольти.

Без `--csv` малює ІЛЮСТРАТИВНУ криву (не реальний замір) — щоб показати форму
залежності (спад із насиченням, а не пряма лінія) і те, які підписи мл
очікувати на осі. Заміни на реальний прогін, коли він буде:

    uv run --with matplotlib tools/plot_calibration_curve.py \
        --csv data/v12-vs-v20-20260822.csv --sensor v20a

Вісь X — мілілітри (кумулятивний долив), вісь Y — мілівольти. Не час: час —
це лише скільки чекати, доки крива всядеться (див. soil_summary.py, --tail),
а сама калібрувальна залежність — це "скільки мВ на скільки мілілітрів".
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from soil_summary import group_by_water, read_rows, sensor_columns, settled, split_references, to_float  # noqa: E402

import statistics

try:
    import matplotlib.pyplot as plt
except ImportError:
    sys.exit("Потрібен matplotlib: uv run --with matplotlib --with scipy tools/plot_calibration_curve.py ...")

try:
    from scipy.interpolate import PchipInterpolator
except ImportError:
    PchipInterpolator = None  # згладжування вимкнено, лінії між точками прямі


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--csv", type=Path, help="CSV від soil_log.py; без нього — ілюстративні дані")
    parser.add_argument(
        "--points",
        help="реальні усталені пари 'мл:мВ,мл:мВ,...' (наприклад '0:2409,50:1018,100:924,150:898') — "
             "малює точно ці точки, без CSV і без ілюстративної кривої",
    )
    parser.add_argument("--sensor", default="v20a", help="мітка сенсора (v12a/v12b/v20a/v20b), типово v20a")
    parser.add_argument("--tail", type=float, default=60.0, help="хвіст кроку в секундах (як у soil_summary.py)")
    parser.add_argument("--dry-mv", type=float, default=2400.0, help="ілюстративний режим: сухий кінець (water=0)")
    parser.add_argument("--wet-mv", type=float, default=1450.0, help="ілюстративний режим: мокра асимптота")
    parser.add_argument("--decay", type=float, default=0.0055, help="ілюстративний режим: швидкість насичення")
    parser.add_argument(
        "--live-point",
        help="точка, що ще пишеться й не всілась: 'мл,мВ' (наприклад '150,876'), малюється окремо",
    )
    parser.add_argument(
        "--project",
        type=int,
        default=0,
        help="скільки ГІПОТЕТИЧНИХ точок домалювати за останньою (пунктиром, не замір) — "
             "екстраполяція геометричним спаданням дельти, узятим з останніх двох реальних кроків",
    )
    parser.add_argument("--project-ratio", type=float, help="коефіцієнт спадання дельти; типово рахується з даних")
    parser.add_argument("--project-step", type=float, help="крок мл для проекції; типово як останній реальний крок")
    parser.add_argument("--live-label", default="ще пишеться", help="підпис для --live-point")
    parser.add_argument("--out", type=Path, default=Path(__file__).resolve().parent.parent / "data" / "calibration_curve.png")
    return parser.parse_args()


def illustrative_data(dry_mv: float, wet_mv: float, decay: float) -> tuple[list[float], list[float]]:
    """Правдоподібна, але вигадана крива: спад мВ з насиченням, не пряма.

    Форма — вологий ґрунт віддає дедалі менше на кожен наступний долив
    (ємнісний зонд насичується), що й відповідає реальній фізиці і тому, що
    видно в справжніх прогонах (нахил спадає ближче до мокрого кінця).
    Кінці (`--dry-mv`/`--wet-mv`) можна підставити реальні заміряні — тоді
    форма між ними лишається ілюстративною, а межі вже не вигадані.
    """
    waters = [0, 50, 100, 150, 200, 300, 400, 500]
    jitter = [3, -2, 4, -3, 2, -4, 3, -1]  # фіксовано, не random() — відтворюваність
    mv = [wet_mv + (dry_mv - wet_mv) * pow(2.718281828, -decay * w) + j for w, j in zip(waters, jitter, strict=True)]
    return [float(w) for w in waters], mv


def measured_data(csv_path: Path, sensor: str, tail: float) -> tuple[list[float], list[float]]:
    header, rows = read_rows(csv_path)
    if not rows:
        sys.exit(f"{csv_path}: немає жодного рядка даних")

    column = f"{sensor}_mv"
    if column not in header:
        sys.exit(f"у {csv_path} нема колонки {column} (є: {', '.join(sensor_columns(header, 'mv'))})")

    _refs, rows = split_references(rows)
    if not rows:
        sys.exit("після довідкових міток немає даних — прогін не почався?")

    steps = group_by_water(rows)
    waters: list[float] = []
    means: list[float] = []
    for water, step_rows in steps.items():
        tail_rows = settled(step_rows, tail)
        values = [to_float(r[column]) for r in tail_rows]
        values = [v for v in values if v is not None]
        if not values:
            continue
        waters.append(water)
        means.append(statistics.fmean(values))
    return waters, means


def main() -> None:
    args = parse_args()

    if args.points:
        pairs = sorted(
            (float(ml), float(mv)) for ml, mv in (p.split(":") for p in args.points.split(","))
        )
        waters, mv = [p[0] for p in pairs], [p[1] for p in pairs]
        title = f"{args.sensor} — calibration curve (measured, settled R15 points)"
    elif args.csv:
        waters, mv = measured_data(args.csv, args.sensor, args.tail)
        title = f"{args.sensor} — calibration curve (measured, {args.csv.name})"
    else:
        waters, mv = illustrative_data(args.dry_mv, args.wet_mv, args.decay)
        title = f"{args.sensor} — calibration curve (illustrative shape, real dry/wet ends)"

    live_ml = live_mv = None
    if args.live_point:
        live_ml_s, live_mv_s = args.live_point.split(",")
        live_ml, live_mv = float(live_ml_s), float(live_mv_s)
        # ще не всілась — не малюємо як частину кривої, тільки як окрему точку.
        waters = [w for w in waters if w != live_ml]
        mv = mv[: len(waters)]

    fig, ax = plt.subplots(figsize=(8, 5), dpi=150)

    if len(waters) >= 3 and PchipInterpolator is not None:
        # PCHIP, не кубічний сплайн: монотонний, без вигинів-«переліт» між
        # точками — крива не малює хибних локальних піків між реальними
        # замірами, тільки згладжує кути.
        curve = PchipInterpolator(waters, mv)
        dense_x = [waters[0] + (waters[-1] - waters[0]) * i / 299 for i in range(300)]
        dense_y = curve(dense_x)
        ax.plot(dense_x, dense_y, color="#2a7f62", linewidth=2, label="settled soil moisture reading")
        ax.plot(waters, mv, marker="o", color="#2a7f62", linewidth=0, markersize=7)
    else:
        ax.plot(waters, mv, marker="o", color="#2a7f62", linewidth=2, markersize=7, label="settled soil moisture reading")

    for w, v in zip(waters, mv, strict=True):
        ax.annotate(f"{w:.0f} mL", xy=(w, v), textcoords="offset points",
                    xytext=(0, 22), ha="center", fontsize=8, color="#555555")

    if live_ml is not None:
        ax.plot(live_ml, live_mv, marker="o", markersize=10, markerfacecolor="none",
                 markeredgecolor="#d17a1f", markeredgewidth=2, linestyle="none", label=args.live_label)
        ax.annotate(f"{live_ml:.0f} mL, {args.live_label}", (live_ml, live_mv),
                    textcoords="offset points", xytext=(10, -14), ha="left", fontsize=8, color="#d17a1f")

    proj_x: list[float] = []
    if args.project > 0:
        # Спадання дельти між останніми двома реальними кроками — те саме
        # насичення, яке видно на кожному кроці кривої (кожен наступний
        # долив рухає показник дедалі менше). Продовжуємо той самий темп
        # ГІПОТЕТИЧНО, не заміром — тому пунктир і окремий колір.
        if len(waters) >= 3:
            d_last = mv[-1] - mv[-2]
            d_prev = mv[-2] - mv[-3]
            ratio = args.project_ratio if args.project_ratio is not None else (
                d_last / d_prev if d_prev else 0.3
            )
        else:
            d_last = -30.0
            ratio = args.project_ratio if args.project_ratio is not None else 0.3
        step = args.project_step if args.project_step is not None else (
            waters[-1] - waters[-2] if len(waters) >= 2 else 50.0
        )

        start_x = live_ml if live_ml is not None else waters[-1]
        start_y = live_mv if live_mv is not None else mv[-1]
        proj_x = [start_x]
        proj_y = [start_y]
        delta = d_last
        for _ in range(args.project):
            delta *= ratio
            proj_x.append(proj_x[-1] + step)
            proj_y.append(proj_y[-1] + delta)

        ax.plot(proj_x, proj_y, linestyle="--", color="#999999", linewidth=1.5,
                 marker="o", markersize=5, markerfacecolor="none", label="projected (hypothetical)")
        for w, v in zip(proj_x[1:], proj_y[1:], strict=True):
            ax.annotate(f"{w:.0f} mL?", xy=(w, v), textcoords="offset points",
                        xytext=(0, -14), ha="center", fontsize=7, color="#999999")

    ax.set_xlabel("Water added, cumulative (mL)")
    ax.set_ylabel("Sensor output (mV)")
    ax.set_title(title, fontsize=11)
    ax.grid(True, linestyle="--", alpha=0.4)
    all_ticks = sorted(set(waters) | ({live_ml} if live_ml is not None else set()) | set(proj_x))
    ax.set_xticks(all_ticks)
    if ax.get_legend_handles_labels()[1]:
        ax.legend(loc="upper right", fontsize=8)
    fig.tight_layout()

    args.out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.out)
    print(f"збережено: {args.out}")


if __name__ == "__main__":
    main()
