#!/usr/bin/env python3
"""Фаза 0: чи псує передача Wi-Fi вимір.

Питання, на яке відповідає: наш шум σ = 0,6 мВ, і саме він дає роздільність
0,15 мл води. Передача Wi-Fi — імпульси 200-300 мА, які просаджують шину
живлення, а від неї живиться і сенсор, і опорна напруга ADC. Якщо σ виросте
втричі, ми проміняли вимірювальний прилад на зручний.

Три відрізки, між ними нічого не чіпати:

    A  nowifi   мережевого коду в прошивці нема взагалі
    B  always   радіо піднято постійно
    C  burst    радіо вимкнене під час заміру, вмикається лише на відправку

Відрізок C і є перевіркою головного рішення в прошивці: чи рятує рознесення
передачі й заміру в часі.

    uv run --with pyserial tools/phase0.py --minutes 10

Скрипт сам заливає прошивки, читає USB і рахує σ. USB тут опорний тракт: він
не залежить від мережі, тож саме йому можна вірити.
"""

import argparse
import contextlib
import os
import signal
import statistics
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

try:
    import serial
except ImportError:
    sys.exit("Потрібен pyserial: uv run --with pyserial tools/phase0.py")

PORT = "/dev/cu.usbserial-0001"
FIRMWARE_DIR = Path(__file__).resolve().parent.parent
DATA_DIR = FIRMWARE_DIR / "data"
MAKE_DIR = FIRMWARE_DIR.parent

SEGMENTS = [
    ("A", "nowifi", None, "мережевого коду нема"),
    ("B", "always", "wifi_field_always", "радіо піднято постійно"),
    ("C", "burst", "wifi_field", "радіо імпульсами"),
]

# Живий сенсор під живленням стоїть тихо. Плаваючий пін дає десятки й сотні
# мілівольт розкиду — саме так ми вже двічі ловили "робочий" мертвий канал.
SANE_MIN_MV = 400
SANE_MAX_MV = 2900
SANE_MAX_SIGMA = 8.0


def detach_from_terminal() -> None:
    """Фоновий процес, який читає /dev/cu.*, отримує SIGTTIN і зупиняється.

    Виглядає це як живий скрипт, що тримає порт і нічого не пише — за минулу
    сесію коштувало трьох втрат даних, двічі непоміченими. Прогін триває
    35 хвилин, тож ризикувати тут нема сенсу.
    """
    with contextlib.suppress(Exception):
        os.setsid()
    for sig in (signal.SIGTTIN, signal.SIGTTOU):
        with contextlib.suppress(Exception):
            signal.signal(sig, signal.SIG_IGN)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--minutes", type=float, default=10.0)
    parser.add_argument("--port", default=PORT)
    parser.add_argument(
        "--skip-flash",
        action="store_true",
        help="не заливати — потрібна прошивка вже стоїть (повтор одного відрізка)",
    )
    return parser.parse_args()


def free_port() -> None:
    """Завислий esptool або логер тримають порт і ламають заливку."""
    subprocess.run(["pkill", "-f", "serial_tee"], capture_output=True)
    out = subprocess.run(["lsof", PORT], capture_output=True, text=True).stdout
    for line in out.splitlines()[1:]:
        pid = line.split()[1]
        subprocess.run(["kill", "-9", pid], capture_output=True)
    time.sleep(2)


def flash(env: str | None) -> None:
    cmd = ["make", "upload", "PROJECT=soil-calibration"]
    if env:
        cmd.append(f"PIO_ENV={env}")
    print(f"   заливаю {env or 'heltec_wifi_lora_32_V3'} ...", flush=True)
    res = subprocess.run(cmd, cwd=MAKE_DIR, capture_output=True, text=True)
    if "SUCCESS" not in res.stdout:
        print(res.stdout[-1500:])
        sys.exit("заливка не вдалась")


def read_labels_and_rows(port: serial.Serial, seconds: float):
    labels: list[str] = []
    rows: list[list[int]] = []
    port.write(b"h\n")
    end = time.time() + seconds
    while time.time() < end:
        line = port.readline().decode("utf-8", "replace").strip()
        if not line:
            continue
        fields = line.split(",")
        if fields[0] == "elapsed_s":
            labels = [f[:-3] for f in fields if f.endswith("_mv")]
            continue
        if line.startswith("#") or not labels:
            continue
        if len(fields) < 3 + 2 * len(labels):
            continue
        try:
            rows.append([int(fields[4 + i * 2]) for i in range(len(labels))])
        except ValueError:
            continue
    return labels, rows


def stats(labels, rows):
    out = {}
    for i, name in enumerate(labels):
        vals = [r[i] for r in rows]
        if len(vals) > 1:
            out[name] = (statistics.fmean(vals), statistics.pstdev(vals), len(vals))
    return out


def live_channels(st):
    """Канали, які схожі на справжній сенсор під живленням."""
    return {n: v for n, v in st.items()
            if SANE_MIN_MV < v[0] < SANE_MAX_MV and v[1] < SANE_MAX_SIGMA}


def main() -> None:
    detach_from_terminal()
    args = parse_args()
    free_port()

    print("=== перевірка перед стартом ===")
    if not args.skip_flash:
        flash(None)
    time.sleep(8)
    with serial.Serial(args.port, 115200, timeout=1) as p:
        time.sleep(1)
        p.reset_input_buffer()
        labels, rows = read_labels_and_rows(p, 12)
    st = stats(labels, rows)
    for n, (m, s, _) in st.items():
        mark = "живий" if n in live_channels(st) else "ПОРОЖНІЙ"
        print(f"   {n:>6} {m:7.1f} мВ  σ={s:6.2f}  {mark}")

    live = live_channels(st)
    if not live:
        sys.exit(
            "\nЖоден канал не схожий на підключений сенсор.\n"
            "Фаза 0 міряє просадку живлення — без живленого сенсора\n"
            "просідати нема чому, і міряти нема що."
        )
    print(f"\n   у грі: {', '.join(live)}\n")

    seconds = args.minutes * 60
    results = {}
    for tag, _name, env, note in SEGMENTS:
        print(f"=== відрізок {tag} — {note} ===")
        if not (args.skip_flash and tag == "A"):
            free_port()
            flash(env)
        time.sleep(8)
        with serial.Serial(args.port, 115200, timeout=1) as p:
            time.sleep(1)
            p.reset_input_buffer()
            print(f"   міряю {args.minutes:.0f} хв ...", flush=True)
            labels, rows = read_labels_and_rows(p, seconds)
        results[tag] = stats(labels, rows)
        for n in live:
            if n in results[tag]:
                m, s, k = results[tag][n]
                print(f"   {n:>6} {m:7.1f} мВ  σ={s:6.2f}  n={k}")
        print()

    print("=" * 58)
    print(f"{'канал':>7} {'A σ':>8} {'B σ':>8} {'C σ':>8} {'B/A':>7} {'C/A':>7}")
    print("-" * 58)
    verdicts = []
    for n in live:
        a = results["A"].get(n)
        b = results["B"].get(n)
        c = results["C"].get(n)
        if not (a and b and c):
            continue
        ba, ca = b[1] / a[1] if a[1] else 0, c[1] / a[1] if a[1] else 0
        verdicts.append((n, ba, ca))
        print(f"{n:>7} {a[1]:8.2f} {b[1]:8.2f} {c[1]:8.2f} {ba:7.1f}× {ca:7.1f}×")
    print("=" * 58)

    if verdicts:
        worst_b = max(v[1] for v in verdicts)
        worst_c = max(v[2] for v in verdicts)
        print()
        if worst_b < 2:
            print("ВИСНОВОК: Wi-Fi нешкідливий, годиться навіть режим ALWAYS")
        elif worst_c < 2:
            print("ВИСНОВОК: ALWAYS псує вимір, BURST рятує — лишаємо штатний режим")
        else:
            print("ВИСНОВОК: Wi-Fi псує вимір навіть імпульсами. "
                  "Для калібрування тракт не годиться.")
        print("\nРівні (не σ) порівнювати не варто: за півгодини вони пливуть "
              "самі, температурний дрейф ~1,5 мВ/год заміряний окремо.")

    stamp = datetime.now().strftime("%Y%m%d-%H%M")
    out = DATA_DIR / f"phase0-{stamp}.txt"
    with open(out, "w", encoding="utf-8") as f:
        f.write(f"# фаза 0, {args.minutes:.0f} хв на відрізок\n")
        for tag, _, _, note in SEGMENTS:
            f.write(f"\n[{tag}] {note}\n")
            for n, (m, s, k) in results[tag].items():
                f.write(f"  {n:>6} {m:8.1f} мВ  σ={s:6.2f}  n={k}\n")
    print(f"\nзбережено: {out}")


if __name__ == "__main__":
    main()
