#!/usr/bin/env python3
"""Безперервний запис Serial у файл. Пише кожен рядок одразу, з flush.

Живе окремим файлом, а не heredoc-ом у фоні: процес, запущений через
`nohup python - <<EOF &`, двічі за сесію зависав намертво — `readline()`
блокувався в ядрі, а не повертав порожнє, тож жоден сторожовий таймер
усередині циклу не спрацьовував. Плата при цьому шле дані справно.

Сторож тут винесено в окремий потік: він дивиться на час останнього рядка
ззовні й перевідкриває порт, не покладаючись на те, що головний цикл
взагалі отримає керування.

    uv run --with pyserial tools/serial_tee.py data/calib-....csv
"""

import contextlib
import sys
import threading
import time

import serial

PORT = "/dev/cu.usbserial-0001"
BAUD = 115200
STALL_S = 25


def main() -> None:
    path = sys.argv[1]
    state = {"last": time.time(), "port": serial.Serial(PORT, BAUD, timeout=2)}
    stop = threading.Event()

    def watchdog():
        while not stop.wait(5):
            if time.time() - state["last"] > STALL_S:
                with contextlib.suppress(Exception):
                    state["port"].close()
                time.sleep(1)
                try:
                    state["port"] = serial.Serial(PORT, BAUD, timeout=2)
                    state["last"] = time.time()
                except Exception:
                    pass

    threading.Thread(target=watchdog, daemon=True).start()

    with open(path, "a", encoding="utf-8") as sink:
        while True:
            try:
                line = state["port"].readline().decode("utf-8", "replace").rstrip()
            except Exception:
                time.sleep(0.5)
                continue
            if line:
                sink.write(line + "\n")
                sink.flush()
                state["last"] = time.time()


if __name__ == "__main__":
    main()
