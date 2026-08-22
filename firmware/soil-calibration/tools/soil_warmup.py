#!/usr/bin/env python3
"""Скільки секунд сенсору треба після подачі живлення, щоб показати правду.

Ганяє на платі команду `c` (зняти живлення -> подати -> швидкі заміри від
t=0), ловить криву й рахує час виходу на полицю для кожного сенсора.

    uv run --with pyserial tools/soil_warmup.py --window 30

Що саме рахується. За «правду» береться **полиця в кінці вікна** (середнє
останньої чверті), і час виходу — це момент, ПІСЛЯ ЯКОГО крива вже не
виходить за допуск до кінця. Саме "після якого", а не "перше влучання":
крива, яка перетнула допуск і поповзла далі, влучає рано й випадково, і
одноразове влучання дало б відповідь у рази оптимістичнішу за правду.

Один зразок за допуском нічого не означає — потрібні кілька підряд (див.
`persist`), інакше випадковий викид ADC відсуває відповідь на десятки
секунд.

Допусків три, бо ціна різна: ±50 мВ — це «видно, мокро чи сухо», ±10 мВ —
«можна калібрувати». Для lowpower-вузла різниця між ними — місяці ресурсу.
"""

import argparse
import csv
import os
import statistics
import sys
import time
from datetime import datetime
from pathlib import Path

try:
    import serial
except ImportError:
    sys.exit("Потрібен pyserial: uv run --with pyserial tools/soil_warmup.py")

DEFAULT_PORT = os.environ.get("SERIAL_PORT", "/dev/cu.usbserial-0001")
DEFAULT_OUT = Path(__file__).resolve().parent.parent / "data"

TOLERANCES_MV = (50, 20, 10)

# Нижче цього сенсор не живий: обрив, нема живлення або пін ні до чого не
# підключений. Такий канал у звіті треба назвати, а не мовчки порахувати.
DEAD_MV = 400


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--window", type=int, default=30, help="секунд кривої")
    parser.add_argument(
        "--off",
        type=int,
        default=0,
        help="секунд без живлення перед кривою (0 = типова пауза прошивки)",
    )
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--save", action="store_true", help="зберегти криву в CSV")
    parser.add_argument(
        "--from-csv",
        type=Path,
        help="перерахувати збережену криву замість нового прогону",
    )
    return parser.parse_args()


def capture(
    port: serial.Serial, window: int, off: int
) -> tuple[list[str], list[list[str]]]:
    """Пускає probe і збирає рядки між його маркерами."""
    port.reset_input_buffer()
    port.write(f"c {window} {off}\n".encode())

    labels: list[str] = []
    rows: list[list[str]] = []
    started = False
    deadline = time.time() + window + off + 25

    while time.time() < deadline:
        line = port.readline().decode("utf-8", "replace").strip()
        if not line:
            continue

        if line.startswith("# --- probe: power cycle ---"):
            started = True
            print("  [живлення знято, чекаю]")
            continue
        if line.startswith("# --- probe: done ---"):
            break
        if line.startswith("#"):
            continue

        fields = line.split(",")
        if fields[0] == "elapsed_s":
            labels = [f[:-3] for f in fields if f.endswith("_mv")]
            continue
        if started and labels and len(fields) >= 3 + 2 * len(labels):
            rows.append(fields)

    return labels, rows


def settle_time(times, values, final: float, tol: float, persist: int = 3):
    """Момент, ПІСЛЯ якого крива вже не виходить за допуск до кінця вікна.

    Саме "після якого", а не "перше влучання": крива, що перетнула допуск і
    поповзла далі, влучає рано й випадково.

    `persist` — скільки зразків підряд мають бути за допуском, щоб це
    зарахувалось за реальний вихід. Без нього один викид шуму відсуває
    відповідь на десятки секунд: на живій кривій рівно так і сталось —
    сенсор устоявся за 0,4 с, а одна точка на 19-й секунді дала "25,8 с".
    Один зразок — це не поведінка сенсора, це шум ADC.
    """
    outside = [abs(v - final) > tol for v in values]

    # Початковий сплеск і пізній викид — різні речі, і фільтр має їх розрізняти.
    # Живлення щойно подали, тож ЛЮБИЙ вихід за допуск на самому початку
    # реальний за визначенням: доводити нічого не треба, ми самі його
    # спричинили. А от одиночний сплеск на 19-й секунді — це шум ADC, і щоб
    # зарахувати його за поведінку сенсора, потрібні `persist` зразків підряд.
    #
    # Без цього поділу метрика бреше в обидва боки: без persist один викид
    # відсуває відповідь на десятки секунд, а з persist на всіх зникає сам
    # підйом, бо він коротший за фільтр (на кроці 0,12 с це 2-3 зразки).
    last_outside = -1
    run = 0
    for i, is_outside in enumerate(outside):
        if is_outside:
            run += 1
            at_start = run == i + 1
            if run >= persist or at_start:
                last_outside = i
        else:
            run = 0

    if last_outside < 0:
        return 0.0
    if last_outside >= len(values) - 1:
        return None  # так і не встоялось у межах вікна
    return times[last_outside + 1]


def main() -> None:
    args = parse_args()

    if args.from_csv:
        # Метрика виходу на полицю мінялась уже раз, і мінятиметься ще —
        # старі криві мають переживати це без повторного прогону на залізі.
        with open(args.from_csv, encoding="utf-8") as handle:
            reader = csv.DictReader(handle)
            saved = list(reader)
        labels = [n[:-3] for n in saved[0] if n.endswith("_mv")]
        times = [float(r["elapsed_s"]) for r in saved]
        series = {n: [int(r[f"{n}_mv"]) for r in saved] for n in labels}
        rows = saved
        print(f"[перерахунок {args.from_csv}]")
    else:
        with serial.Serial(args.port, args.baud, timeout=2) as port:
            time.sleep(2)  # плата щойно могла ресетнутись від відкриття порту
            print(f"[probe {args.window} c на {args.port}]")
            labels, rows = capture(port, args.window, args.off)

        if not rows:
            sys.exit("плата не віддала криву — прошивка з командою `c` залита?")

        times = [float(r[0]) for r in rows]
        series = {
            label: [int(r[4 + i * 2]) for r in rows] for i, label in enumerate(labels)
        }

    if args.save and not args.from_csv:
        args.out.mkdir(parents=True, exist_ok=True)
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        path = args.out / f"warmup-off{args.off or 3}s-{stamp}.csv"
        with open(path, "w", encoding="utf-8") as handle:
            handle.write("elapsed_s," + ",".join(f"{n}_mv" for n in labels) + "\n")
            for i, t in enumerate(times):
                handle.write(
                    f"{t}," + ",".join(str(series[n][i]) for n in labels) + "\n"
                )
        print(f"[крива збережена: {path}]")

    print(f"\nточок: {len(rows)}, вікно: {times[-1]:.1f} c, крок ~{
        (times[-1] - times[0]) / max(1, len(times) - 1):.2f} c\n")

    head = "сенсор".rjust(7) + "старт".rjust(9) + "полиця".rjust(9) + "стрибок".rjust(9)
    head += "".join(f"±{t}мВ".rjust(9) for t in TOLERANCES_MV)
    print(head)
    print("-" * len(head))

    for label in labels:
        values = series[label]
        tail = values[len(values) * 3 // 4 :]
        final = statistics.fmean(tail)

        row = f"{label:>7}{values[0]:9d}{final:9.0f}{values[0] - final:+9.0f}"

        if final < DEAD_MV:
            print(row + "   не живий (обрив / нема живлення)")
            continue

        for tol in TOLERANCES_MV:
            t_settle = settle_time(times, values, final, tol)
            row += ("нема" if t_settle is None else f"{t_settle:.1f}c").rjust(9)
        print(row)

    print("\nстарт — перший замір після подачі; полиця — середнє останньої чверті;")
    print("колонки допусків — після якої секунди крива вже не виходить за нього.")


if __name__ == "__main__":
    main()
