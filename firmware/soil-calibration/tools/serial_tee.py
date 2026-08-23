#!/usr/bin/env python3
"""Безперервний запис Serial у файл. Пише кожен рядок одразу, з flush.

ГОЛОВНЕ, чому цей файл узагалі існує окремо: `/dev/cu.*` — це термінальний
пристрій, і фоновий процес, який з нього читає, отримує від ядра **SIGTTIN**
і зупиняється. Зовні це виглядає як «логер завис»: процес живий, порт
тримає, плата шле — а файл не росте. За сесію 2026-08-22 воно спрацювало
тричі, і двічі ми через це не бачили реакцію ґрунту на долив.

Лікується двома рядками: `os.setsid()` (вийти з групи процесів термінала) і
ігнорування SIGTTIN/SIGTTOU. Обидва — до відкриття порту.

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
import os
import signal
import sys
import threading
import time

import serial

PORT = "/dev/cu.usbserial-0001"
BAUD = 115200
STALL_S = 25


def detach_from_terminal() -> None:
    """Без цього фонове читання з /dev/cu.* зупиняє процес по SIGTTIN."""
    with contextlib.suppress(Exception):
        os.setsid()
    for sig in (signal.SIGTTIN, signal.SIGTTOU):
        with contextlib.suppress(Exception):
            signal.signal(sig, signal.SIG_IGN)


def main() -> None:
    detach_from_terminal()
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
