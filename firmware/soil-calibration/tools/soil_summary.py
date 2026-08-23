#!/usr/bin/env python3
"""Підсумок калібрувального прогону: скільки ADC коштує один долив води.

Читає CSV, який записав soil_log.py, і зводить його до таблиці "порція води ->
показник кожного сенсора". Тільки стандартна бібліотека, щоб запускалось без
жодного встановлення.

    python3 tools/soil_summary.py data/versions-20260822-181500.csv

Ключове рішення тут — БРАТИ НЕ ВСІ РЯДКИ КРОКУ, А ХВІСТ. Одразу після доливу
вода ще розходиться по ґрунту, і сенсор кілька десятків секунд показує
перехідний процес. Якби ми усереднили крок цілком, у середнє потрапила б
половина цього переходу, і крива вийшла б згладженою неправдою. Тому за
значення кроку береться середнє останніх --tail секунд перед наступним
доливом — те, на чому сенсор устоявся.

Окрема колонка — шум усередині кроку (σ). Якщо крок дає зсув менший за 2-3 σ,
то ця порція води просто не видно сенсору, і лити треба більшими.
"""

import argparse
import csv
import statistics
import sys
from collections import OrderedDict
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("path", type=Path, help="CSV від soil_log.py")
    parser.add_argument(
        "--tail",
        type=float,
        default=60.0,
        help="секунд у хвості кроку, які вважаємо усталеними (типово 60)",
    )
    parser.add_argument(
        "--column",
        choices=["raw", "mv"],
        default="raw",
        help="що зводити: сире ADC (типово) чи мілівольти",
    )
    return parser.parse_args()


def read_rows(path: Path) -> tuple[list[str], list[dict]]:
    """Повертає (заголовок, рядки). Коментарі '#' пропускаються.

    Заголовок може трапитись у файлі не один раз — команда `r` на платі
    починає новий прогін і друкує його заново. Беремо перший, решту
    відкидаємо: колонки однакові, а от порахувати заголовок як дані було б
    тихою помилкою.
    """
    header: list[str] = []
    rows: list[dict] = []

    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#"):
                continue

            fields = next(csv.reader([line]))

            if fields[0] == "elapsed_s":
                if not header:
                    header = fields
                continue

            if not header or len(fields) != len(header):
                continue

            rows.append(dict(zip(header, fields, strict=True)))

    return header, rows


def to_float(value: str) -> float | None:
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


REFERENCE_EVENTS = ("dry_ref", "wet_ref")


def split_references(rows: list[dict]) -> tuple[list[dict], list[dict]]:
    """Відділяє довідкові точки від кривої води.

    `dry_ref` (повітря) і `wet_ref` (склянка води) знімаються ДО того, як
    сенсори опиняться в ґрунті — це межі самого сенсора, а не ґрунту. У них
    `water_ml` теж нуль, тож без цього розділення десятки секунд заміру в
    повітрі втекли б у нульовий крок і зіпсували найважливішу точку прогону.

    Межа — ОСТАННЯ довідкова мітка: усе до неї належить довідковій фазі.
    """
    last_ref = -1
    for i, row in enumerate(rows):
        if row.get("event") in REFERENCE_EVENTS:
            last_ref = i

    if last_ref < 0:
        return [], rows

    refs = [r for r in rows[: last_ref + 1] if r.get("event") in REFERENCE_EVENTS]
    return refs, rows[last_ref + 1 :]


def group_by_water(rows: list[dict]) -> "OrderedDict[float, list[dict]]":
    steps: OrderedDict[float, list[dict]] = OrderedDict()
    for row in rows:
        water = to_float(row.get("water_ml", ""))
        if water is None:
            continue
        steps.setdefault(water, []).append(row)
    return steps


def settled(step_rows: list[dict], tail_s: float) -> list[dict]:
    """Останні tail_s секунд кроку — те, на чому сенсор устоявся."""
    times = [to_float(r["elapsed_s"]) for r in step_rows]
    times = [t for t in times if t is not None]
    if not times:
        return step_rows

    cutoff = max(times) - tail_s
    tail = [r for r in step_rows if (to_float(r["elapsed_s"]) or 0) >= cutoff]
    return tail or step_rows


def sensor_columns(header: list[str], suffix: str) -> list[str]:
    return [name for name in header if name.endswith(f"_{suffix}")]


def linear_fit(xs: list[float], ys: list[float]) -> tuple[float, float]:
    """Найменші квадрати: повертає (нахил, R²). Нахил — одиниць ADC на 1 мл."""
    n = len(xs)
    if n < 2:
        return 0.0, 0.0

    mean_x = statistics.fmean(xs)
    mean_y = statistics.fmean(ys)
    sxx = sum((x - mean_x) ** 2 for x in xs)
    sxy = sum((x - mean_x) * (y - mean_y) for x, y in zip(xs, ys, strict=True))
    if sxx == 0:
        return 0.0, 0.0

    slope = sxy / sxx
    syy = sum((y - mean_y) ** 2 for y in ys)
    r2 = (sxy**2 / (sxx * syy)) if syy else 0.0
    return slope, r2


def main() -> None:
    args = parse_args()

    header, rows = read_rows(args.path)
    if not rows:
        sys.exit(f"{args.path}: немає жодного рядка даних")

    columns = sensor_columns(header, args.column)
    if not columns:
        sys.exit(f"у заголовку немає колонок *_{args.column}")

    refs, rows = split_references(rows)
    if not rows:
        sys.exit("після довідкових міток немає даних — прогін не почався?")

    steps = group_by_water(rows)

    print(f"файл:   {args.path}")
    print(f"рядків: {len(rows)}, кроків води: {len(steps)}, хвіст: {args.tail:.0f} с")
    print()

    if refs:
        width_ref = max(14, max(len(c) for c in columns) + 2)
        print("крайні точки сенсорів (повітря / вода, не ґрунт):")
        print("     мітка" + "".join(c.rjust(width_ref) for c in columns))
        by_event: dict[str, dict[str, float | None]] = {}
        for row in refs:
            by_event[row["event"]] = {c: to_float(row[c]) for c in columns}
            cells = "".join(
                f"{v:.0f}".rjust(width_ref) if v is not None else "-".rjust(width_ref)
                for v in by_event[row["event"]].values()
            )
            print(f"{row['event']:>10}" + cells)

        # Розмах повітря-вода показує, скільки сенсор узагалі здатен віддати.
        # Саме він стиснутий у v1.2 на 3,3 В — претензія з power-budget.
        if set(REFERENCE_EVENTS) <= by_event.keys():
            spans = []
            for name in columns:
                dry, wet = by_event["dry_ref"][name], by_event["wet_ref"][name]
                spans.append(
                    f"{abs(dry - wet):.0f}".rjust(width_ref)
                    if dry is not None and wet is not None
                    else "-".rjust(width_ref)
                )
            print("     розмах" + "".join(spans))
        print()

    # Середнє й σ на кожному кроці. σ рахуємо по тому ж хвосту: це шум спокою,
    # а не розмах перехідного процесу.
    means: dict[str, list[float]] = {name: [] for name in columns}
    sigmas: dict[str, list[float]] = {name: [] for name in columns}
    waters: list[float] = []

    for water, step_rows in steps.items():
        tail_rows = settled(step_rows, args.tail)
        waters.append(water)
        for name in columns:
            values = [to_float(r[name]) for r in tail_rows]
            values = [v for v in values if v is not None]
            means[name].append(statistics.fmean(values) if values else float("nan"))
            sigmas[name].append(statistics.stdev(values) if len(values) > 1 else 0.0)

    width = max(14, max(len(c) for c in columns) + 2)
    print("вода, мл".rjust(9) + "  n" + "".join(c.rjust(width) for c in columns))
    print("-" * (12 + width * len(columns)))

    for i, water in enumerate(waters):
        cells = []
        for name in columns:
            value = means[name][i]
            delta = "" if i == 0 else f" ({value - means[name][i - 1]:+.0f})"
            cells.append(f"{value:.0f}{delta}".rjust(width))
        n = len(settled(steps[water], args.tail))
        print(f"{water:9.0f}{n:3d}" + "".join(cells))

    print()
    print("нахил (одиниць на 1 мл), R², типовий шум σ:")
    for name in columns:
        slope, r2 = linear_fit(waters, means[name])
        noise = statistics.fmean(sigmas[name])
        # Крок буде видно тільки якщо він вилазить за шум. Три σ — звична межа.
        min_ml = (3 * noise / abs(slope)) if slope else float("inf")
        print(
            f"  {name:>10}: {slope:+8.2f}   R²={r2:5.3f}   σ={noise:6.1f}"
            f"   мінімальна помітна порція ≈ {min_ml:.0f} мл"
        )


if __name__ == "__main__":
    main()
