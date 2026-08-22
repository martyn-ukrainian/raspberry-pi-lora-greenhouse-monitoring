#!/usr/bin/env python3
"""Заміряти один сенсор після заміни: скільки встоювався і на чому зупинився.

Робочий канал на стенді один (GPIO2), сенсорів чотири — тож порівнюємо їх не
поруч, а по черзі в тому самому каналі, у тій самій ямці, в тому самому
ґрунті. Це сильніше за чотири канали одразу: різниця ямки й глибини не
змішується з різницею сенсорів.

    uv run --with pyserial tools/soil_swap.py --name v1.2-A --seconds 60

Заміна сенсора — це і зняття живлення з нього, тож той самий прогін заодно
міряє холодний старт: скільки секунд від встромляння до правдивого числа.

Результат кожного виклику дописується в data/swap-session.csv і таблиця
порівняння друкується заново — тобто після четвертого сенсора відповідь уже
на екрані, зводити нічого не треба.
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
    sys.exit("Потрібен pyserial: uv run --with pyserial tools/soil_swap.py")

DEFAULT_PORT = os.environ.get("SERIAL_PORT", "/dev/cu.usbserial-0001")
DEFAULT_OUT = Path(__file__).resolve().parent.parent / "data"
SESSION = "swap-session.csv"

TOLERANCES_MV = (50, 20, 10)
DEAD_MV = 400

FIELDS = [
    "name", "channel", "when", "n", "first_mv", "final_mv", "final_raw",
    "sigma_mv", "settle_50", "settle_20", "settle_10",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True, help="який сенсор зараз стоїть")
    parser.add_argument("--seconds", type=int, default=60)
    parser.add_argument("--channel", default="v12a", help="мітка каналу в CSV")
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    return parser.parse_args()


def capture(port: serial.Serial, channel: str, seconds: int):
    """Збирає (час, raw, mv) вказаного каналу протягом seconds."""
    # Плата вже працює, заголовка ми не бачили — просимо перевидати. Саме `h`,
    # а не `r`: скидати прогін посеред сесії не можна.
    port.reset_input_buffer()
    port.write(b"h\n")

    labels: list[str] = []
    samples: list[tuple[float, int, int]] = []
    deadline = time.time() + seconds + 15
    started = 0.0

    while time.time() < deadline:
        line = port.readline().decode("utf-8", "replace").strip()
        if not line:
            continue

        fields = line.split(",")
        if fields[0] == "elapsed_s":
            labels = [f[:-3] for f in fields if f.endswith("_mv")]
            continue
        if line.startswith("#") or not labels:
            continue
        if channel not in labels:
            sys.exit(f"каналу {channel} немає; є: {', '.join(labels)}")

        index = labels.index(channel)
        if len(fields) < 5 + index * 2:
            continue

        try:
            raw = int(fields[3 + index * 2])
            mv = int(fields[4 + index * 2])
        except ValueError:
            continue

        now = time.time()
        if not samples:
            started = now
        samples.append((now - started, raw, mv))

        if now - started >= seconds:
            break

        left = int(seconds - (now - started))
        print(f"\r  {mv:5d} mV   лишилось {left:3d} c ", end="", flush=True)

    print()
    return samples


def settle_time(samples, final: float, tol: float, persist: int = 3):
    """Момент, ПІСЛЯ якого крива вже не виходить за допуск до кінця вікна.

    Саме "після якого", а не "перше влучання": крива, що перетнула допуск і
    поповзла далі, влучає рано й випадково.

    `persist` — скільки зразків підряд мають бути за допуском, щоб це
    зарахувалось за реальний вихід. Без нього один викид шуму відсуває
    відповідь на десятки секунд: на живій кривій рівно так і сталось —
    сенсор устоявся за 0,4 с, а одна точка на 19-й секунді дала "25,8 с".
    Один зразок — це не поведінка сенсора, це шум ADC.
    """
    outside = [abs(mv - final) > tol for _, _, mv in samples]

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
    if last_outside >= len(samples) - 1:
        return None  # так і не встоялось у межах вікна
    return samples[last_outside + 1][0]


def append_session(path: Path, row: dict) -> list[dict]:
    exists = path.exists()
    with open(path, "a", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS)
        if not exists:
            writer.writeheader()
        writer.writerow(row)

    with open(path, encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def show(value: str) -> str:
    return "нема" if value in ("", "None") else value


def main() -> None:
    args = parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    print(f"[{args.name}] міряю {args.seconds} c на каналі {args.channel}")
    with serial.Serial(args.port, args.baud, timeout=2) as port:
        samples = capture(port, args.channel, args.seconds)

    if len(samples) < 5:
        sys.exit("замало точок — плата шле дані?")

    # Полиця — хвіст вікна: якщо сенсор ще повзе, середнє по всьому вікну
    # затягло б у результат сам перехідний процес.
    tail = samples[len(samples) * 3 // 4 :]
    final_mv = statistics.fmean(s[2] for s in tail)
    final_raw = statistics.fmean(s[1] for s in tail)
    sigma = statistics.pstdev([s[2] for s in tail]) if len(tail) > 1 else 0.0

    settles = {tol: settle_time(samples, final_mv, tol) for tol in TOLERANCES_MV}

    row = {
        "name": args.name,
        "channel": args.channel,
        "when": datetime.now().isoformat(timespec="seconds"),
        "n": len(samples),
        "first_mv": samples[0][2],
        "final_mv": f"{final_mv:.0f}",
        "final_raw": f"{final_raw:.0f}",
        "sigma_mv": f"{sigma:.1f}",
        "settle_50": "" if settles[50] is None else f"{settles[50]:.1f}",
        "settle_20": "" if settles[20] is None else f"{settles[20]:.1f}",
        "settle_10": "" if settles[10] is None else f"{settles[10]:.1f}",
    }

    if final_mv < DEAD_MV:
        print(f"\n  {final_mv:.0f} mV — канал не живий, сенсор не рахую")
        return

    print(f"\n  перший замір : {samples[0][2]} mV")
    print(f"  полиця       : {final_mv:.0f} mV (raw {final_raw:.0f}), σ={sigma:.1f}")
    print(f"  стрибок      : {samples[0][2] - final_mv:+.0f} mV")
    for tol in TOLERANCES_MV:
        value = settles[tol]
        text = "не встоявся за вікно" if value is None else f"{value:.1f} c"
        print(f"  вийшов ±{tol:<3d} мВ : {text}")

    rows = append_session(args.out / SESSION, row)

    print("\n" + "=" * 66)
    print(f"{'сенсор':>10}{'полиця':>9}{'σ':>6}{'±50мВ':>8}{'±20мВ':>8}{'±10мВ':>8}")
    print("-" * 66)
    for entry in rows:
        print(
            f"{entry['name']:>10}{entry['final_mv'] + ' mV':>9}{entry['sigma_mv']:>6}"
            f"{show(entry['settle_50']):>8}{show(entry['settle_20']):>8}"
            f"{show(entry['settle_10']):>8}"
        )
    print("=" * 66)

    if len(rows) > 1:
        values = [float(e["final_mv"]) for e in rows]
        print(f"розкид між сенсорами: {max(values) - min(values):.0f} мВ")


if __name__ == "__main__":
    main()
