#!/usr/bin/env python3
"""Логер калібрувального стенду: пише CSV з плати у файл і пускає команди назад.

Плата (firmware/soil-calibration) друкує в Serial готовий CSV, тож завдання
скрипта скромне: відкрити порт, зберегти кожен рядок у файл і одночасно
дозволити набирати команди ("50" = долив 50 мл) — щоб момент доливу опинився
в тих самих даних, а не в окремому блокноті.

Пишемо на диск ОДРАЗУ, з flush на кожен рядок. Прогін триває годинами, і
втратити його через закритий ноут або висмикнутий USB було б прикро.

    uv run --with pyserial tools/soil_log.py --run versions

Або через Makefile у firmware/: `make soil-log RUN=versions`.
"""

import argparse
import os
import sys
import threading
from datetime import datetime
from pathlib import Path

try:
    import serial
except ImportError:
    sys.exit("Потрібен pyserial: uv run --with pyserial tools/soil_log.py ...")

DEFAULT_PORT = os.environ.get("SERIAL_PORT", "/dev/cu.usbserial-0001")
DEFAULT_OUT = Path(__file__).resolve().parent.parent / "data"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=DEFAULT_PORT, help="USB-порт плати")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument(
        "--run",
        default="run",
        help="ім'я прогону; іде в назву файлу разом з датою",
    )
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT, help="куди класти CSV")
    return parser.parse_args()


def pump_serial(port: serial.Serial, sink, stop: threading.Event) -> None:
    """Читає порт до кінця сеансу: рядок -> файл + екран."""
    while not stop.is_set():
        try:
            raw = port.readline()
        except serial.SerialException as exc:
            print(f"\n[порт відвалився: {exc}]")
            stop.set()
            return

        if not raw:
            continue

        # errors="replace": шум на лінії при підключенні не має вбивати прогін.
        line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
        sink.write(line + "\n")
        sink.flush()
        print(line)


def main() -> None:
    args = parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    path = args.out / f"{args.run}-{stamp}.csv"

    started = datetime.now().isoformat(timespec="seconds")

    with (
        serial.Serial(args.port, args.baud, timeout=1) as port,
        open(path, "w", encoding="utf-8") as sink,
    ):
        sink.write(f"# run={args.run} started={started}\n")

        print(f"[пишу {path}]")
        print("[набери число мілілітрів + Enter після кожного доливу, 'q' — вихід]")

        stop = threading.Event()
        reader = threading.Thread(
            target=pump_serial, args=(port, sink, stop), daemon=True
        )
        reader.start()

        try:
            for line in sys.stdin:
                if stop.is_set():
                    break
                command = line.strip()
                if command.lower() == "q":
                    break
                if command:
                    port.write((command + "\n").encode())
        except KeyboardInterrupt:
            pass
        finally:
            stop.set()
            reader.join(timeout=2)

    print(f"\n[записано {path}]")


if __name__ == "__main__":
    main()
