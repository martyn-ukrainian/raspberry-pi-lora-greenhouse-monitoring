#!/usr/bin/env python3
"""Майстер налаштування Wi-Fi: обрати мережу зі списку, залити, перевірити.

Прибирає рівно ті помилки, на яких ми спотикались за сесію 2026-08-23:

- **ім'я набране руками.** Реальне ім'я телефона було `Martyn — iPhone` — з
  довгим тире, яке ламає прапорець збірки. Через mDNS воно ж приходило
  санітизованим як `Martyn-iPhone`, тобто виглядало безпечним. Тут ім'я
  береться зі сканування як є, набирати нічого;
- **5 ГГц.** ESP32 бачить лише 2,4 ГГц. Мережа на 5 ГГц виглядає точнісінько
  як неправильний пароль — плата просто не знаходить її. Скан показує канал,
  тож 5 ГГц відсіюється до заливки, а не після;
- **«здається, працює».** Після заливки скрипт читає USB і чекає рядок про
  підключення. Або IP, або діагноз — без «ну начебто».

Чого майстер НЕ робить: не дістає пароль (його нема звідки взяти — див.
WIFI.md) і не підключає Mac до хотспота. Друге зайве: пароль перевіряє сама
плата, а Mac при цьому вилетів би з домашньої мережі разом із приймачем.

    uv run --with pyserial tools/wifi_setup.py
"""

import argparse
import re
import subprocess
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError:
    sys.exit("Потрібен pyserial: uv run --with pyserial tools/wifi_setup.py")

FIRMWARE_DIR = Path(__file__).resolve().parent.parent
MAKE_DIR = FIRMWARE_DIR.parent
ENV_PATH = MAKE_DIR / ".env"
PORT = "/dev/cu.usbserial-0001"


def scan() -> list[dict]:
    """Мережі навколо: ім'я, канал, діапазон, захист."""
    out = subprocess.run(
        ["system_profiler", "SPAirPortDataType"], capture_output=True, text=True
    ).stdout

    nets: list[dict] = []
    current: dict = {}
    in_others = False
    for line in out.splitlines():
        if "Other Local Wi-Fi Networks:" in line:
            in_others = True
            continue
        if "Current Network Information:" in line:
            in_others = True
            continue
        if not in_others:
            continue

        name = re.match(r"^ {12}(\S.*?):\s*$", line)
        if name:
            if current.get("ssid"):
                nets.append(current)
            current = {"ssid": name.group(1)}
            continue
        ch = re.search(r"Channel:\s*(\d+)\s*\((\d)GHz", line)
        if ch and current:
            current["channel"] = int(ch.group(1))
            current["band"] = f"{ch.group(2)}GHz"
        sec = re.search(r"Security:\s*(.+?)\s*$", line)
        if sec and current:
            current["security"] = sec.group(1)
    if current.get("ssid"):
        nets.append(current)
    return nets


def ssid_warnings(ssid: str) -> list[str]:
    problems = []
    if not ssid.isascii():
        bad = [c for c in ssid if not c.isascii()]
        problems.append(
            f"не-ASCII символи ({' '.join(bad)}) — зламають прапорець"
        )
    if " " in ssid:
        problems.append("пробіли в імені — можливі проблеми з екрануванням")
    if '"' in ssid or "\\" in ssid:
        problems.append("лапки або зворотний слеш — точно зламають")
    return problems


def write_env(ssid: str, password: str) -> None:
    lines = (
        ENV_PATH.read_text(encoding="utf-8").splitlines()
        if ENV_PATH.exists()
        else []
    )
    seen = {"WIFI_SSID": False, "WIFI_PASS": False}
    out = []
    for line in lines:
        if line.startswith("WIFI_SSID="):
            out.append(f"WIFI_SSID={ssid}")
            seen["WIFI_SSID"] = True
        elif line.startswith("WIFI_PASS="):
            out.append(f"WIFI_PASS={password}")
            seen["WIFI_PASS"] = True
        else:
            out.append(line)
    if not seen["WIFI_SSID"]:
        out.append(f"WIFI_SSID={ssid}")
    if not seen["WIFI_PASS"]:
        out.append(f"WIFI_PASS={password}")
    ENV_PATH.write_text("\n".join(out) + "\n", encoding="utf-8")


def free_port() -> None:
    subprocess.run(["pkill", "-f", "serial_tee"], capture_output=True)
    out = subprocess.run(["lsof", PORT], capture_output=True, text=True).stdout
    for line in out.splitlines()[1:]:
        subprocess.run(["kill", "-9", line.split()[1]], capture_output=True)
    time.sleep(2)


def flash(env: str) -> bool:
    print(f"\nзаливаю {env} ...", flush=True)
    res = subprocess.run(
        ["make", "upload", "PROJECT=soil-calibration", f"PIO_ENV={env}"],
        cwd=MAKE_DIR, capture_output=True, text=True,
    )
    if "SUCCESS" not in res.stdout:
        print(res.stdout[-1200:])
        return False
    return True


def verify(seconds: int = 30) -> None:
    """Плата сама скаже, підключилась вона чи ні — слухаємо її, не гадаємо."""
    print("слухаю плату ...\n")
    with serial.Serial(PORT, 115200, timeout=1) as p:
        end = time.time() + seconds
        while time.time() < end:
            line = p.readline().decode("utf-8", "replace").strip()
            if not line.startswith("#"):
                continue
            print("  ", line[:100])
            if "Wi-Fi підключено" in line:
                print("\n✅ мережа піднялась, пароль правильний")
                return
            if "Wi-Fi недоступний" in line:
                print("\n❌ не підключилась. Найімовірніші причини, за частотою:")
                print("   1. мережа на 5 ГГц — ESP32 бачить лише 2,4")
                print("   2. неправильний пароль")
                print("   3. хотспот згас — iPhone гасить його без клієнтів")
                return
    print("\n⚠️  плата нічого не сказала про Wi-Fi за 30 с")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--env", default="wifi_field",
                        choices=["wifi_field", "wifi_field_always"])
    args = parser.parse_args()

    print("сканую мережі ...\n")
    nets = [n for n in scan() if n.get("ssid")]
    if not nets:
        sys.exit("жодної мережі не видно — Wi-Fi на Mac увімкнений?")

    for i, n in enumerate(nets, 1):
        band = n.get("band", "?")
        mark = "" if band == "2GHz" else "  ← ESP32 НЕ ПОБАЧИТЬ"
        print(f"  {i:2}. {n['ssid']:<28} {band:>6} ch{n.get('channel','?'):<4}"
              f" {n.get('security','')}{mark}")

    print("\nЯкщо хотспота телефона нема в списку — увімкни роздачу й перезапусти.")
    choice = input("\nномер мережі: ").strip()
    try:
        net = nets[int(choice) - 1]
    except (ValueError, IndexError):
        sys.exit("немає такого номера")

    ssid = net["ssid"]
    if net.get("band") != "2GHz":
        print(f"\n⚠️  {ssid} на {net.get('band')} — ESP32 працює лише на 2,4 ГГц.")
        print("   На iPhone: Режим модема → Максимальна сумісність.")
        if input("   усе одно продовжити? [y/N] ").lower() != "y":
            sys.exit("скасовано")

    for w in ssid_warnings(ssid):
        print(f"\n⚠️  {w}")

    password = input(f"\nпароль для «{ssid}»: ").strip()
    if len(password) < 8:
        sys.exit("WPA2 вимагає щонайменше 8 символів — коротший телефон не прийме")

    write_env(ssid, password)
    print(f"\nзаписано в {ENV_PATH}")

    free_port()
    if not flash(args.env):
        sys.exit("заливка не вдалась")
    time.sleep(8)
    verify()


if __name__ == "__main__":
    main()
