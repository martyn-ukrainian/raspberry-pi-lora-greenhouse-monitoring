#!/usr/bin/env python3
"""Живий монітор для етапу «підключаю дроти».

CSV з плати читається очима погано: тринадцять колонок, і поки знайдеш, який
стовпець це GPIO4, уже забув, що робив з дротом. Тут те саме, але по одному
сенсору в рядок, зі шкалою і присудом.

    uv run --with pyserial tools/soil_watch.py

Присуд рахується з поведінки за останні ~10 замірів, а не з одного значення,
бо саме поведінка й відрізняє плаваючий пін від сухого сенсора:

    OK       - тримається на місці в робочому діапазоні
    ПЛАВАЄ   - повзе або теліпається: пін ні до чого не підключений
    НУЛЬ     - лежить біля землі: нема дроту або нема живлення сенсора
    ?        - у робочому діапазоні, але хитається сильніше за очікуване
"""

import argparse
import os
import statistics
import sys
from collections import defaultdict, deque

try:
    import serial
except ImportError:
    sys.exit("Потрібен pyserial: uv run --with pyserial tools/soil_watch.py")

DEFAULT_PORT = os.environ.get("SERIAL_PORT", "/dev/cu.usbserial-0001")

# Живий ємнісний сенсор на 3,3 В не виходить за ці межі ні в повітрі, ні у
# склянці води. Усе, що нижче, — це не «дуже мокро», а обрив або нема живлення.
DEAD_MV = 400
WET_MV = 1100
DRY_MV = 2900

HISTORY = 10
# Сенсор у спокої стоїть в межах одиниць мВ; десятки — це вже не шум ADC.
STABLE_MV = 15


def verdict(values: deque[int]) -> str:
    last = values[-1]
    if last < DEAD_MV:
        return "НУЛЬ    нема дроту або нема живлення"

    if len(values) < HISTORY:
        return "..."

    spread = max(values) - min(values)
    drift = values[-1] - values[0]

    # Плаваючий пін видає себе не рівнем, а рухом: він або монотонно сповзає,
    # або теліпається. Сенсор у повітрі не робить ні того, ні іншого.
    if abs(drift) > STABLE_MV * 2 or spread > STABLE_MV * 3:
        return "ПЛАВАЄ  пін ні до чого не підключений"

    if last < WET_MV:
        return "OK      мокро"
    if last > DRY_MV:
        return "OK      дуже сухо / межа"
    return "OK      сухо (повітря)"


def bar(mv: int, width: int = 20) -> str:
    filled = max(0, min(width, round(mv / 3300 * width)))
    return "#" * filled + "." * (width - filled)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()

    labels: list[str] = []
    history: dict[str, deque[int]] = defaultdict(lambda: deque(maxlen=HISTORY))

    print(f"[{args.port}] Ctrl-C — вихід")

    with serial.Serial(args.port, args.baud, timeout=2) as port:
        while True:
            line = port.readline().decode("utf-8", "replace").strip()
            if not line:
                continue

            fields = line.split(",")

            # Заголовок несе розкладку колонок — з нього і беремо мітки, щоб
            # монітор не знав наперед ні кількості сенсорів, ні їхніх імен.
            if fields[0] == "elapsed_s":
                labels = [f[:-3] for f in fields if f.endswith("_mv")]
                continue

            if not labels or len(fields) < 3 + 2 * len(labels):
                if line.startswith("#"):
                    print(line)
                continue

            for i, label in enumerate(labels):
                try:
                    history[label].append(int(fields[4 + i * 2]))
                except ValueError:
                    continue

            print("\033[2J\033[H", end="")  # очистити екран, курсор угору
            print(f"t = {fields[0]} c\n")
            for label in labels:
                values = history[label]
                if not values:
                    continue
                mv = values[-1]
                jitter = round(statistics.pstdev(values)) if len(values) > 1 else 0
                row = f"{label:>6}  {mv:5d} mV  |{bar(mv)}|  ±{jitter:<3d}"
                print(f"{row}  {verdict(values)}")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print()
