#!/usr/bin/env python3
"""Міст PI → хмара: штовхає нові виміри вузлів у Neon, щоб їх було видно з
телефона по LTE через Vercel (сторінка /nodes).

Навіщо окремо, а не запис із самого сервера: Vercel не бачить SQLite на PI
(приватна мережа за NAT), тож дані треба перекласти в хмарну базу. Форвардер —
окремий процес, який лише ЧИТАЄ agro.db (SELECT) і POST-ить пачку на Vercel.
Якщо він упаде — локальні /live і /stage3 працюють; наздожене з курсора.

Курсор (останній надісланий measurement.id) лежить у .forward_cursor поруч,
щоб після перезапуску не слати все заново й не дублювати (сервер усе одно
дедуплікує за source_id, але курсор економить трафік).

Дані ті самі, що в measurement: node_id, air_t/h, soil(+raw), rssi, snr,
vbat, uptime, timestamp. Мітки/пороги вузлів (з config) шле раз на старт, щоб
хмарна сторінка теж могла фарбувати за нормою.

    uv run python node_forwarder.py            # разовий прогін (для перевірки)
    uv run python node_forwarder.py --loop     # демон (systemd)

Змінні (з ~/agro/server/.env або оточення):
    NODES_INGEST_URL   https://<проєкт>.vercel.app/api/nodes/ingest
    NODES_TOKEN        той самий, що в Vercel
"""

from __future__ import annotations

import argparse
import os
import sqlite3
import sys
import time
from pathlib import Path

import httpx

HERE = Path(__file__).resolve().parent
DB_PATH = HERE / "agro.db"
CURSOR_PATH = HERE / ".forward_cursor"
BATCH_LIMIT = 500
LOOP_SECONDS = 15


def load_env() -> None:
    """Читаємо .env поруч без зайвих залежностей: KEY=VALUE, # коментарі."""
    env = HERE / ".env"
    if not env.exists():
        return
    for line in env.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, val = line.split("=", 1)
        os.environ.setdefault(key.strip(), val.strip().strip('"').strip("'"))


def read_cursor() -> int:
    try:
        return int(CURSOR_PATH.read_text().strip())
    except (OSError, ValueError):
        return 0


def write_cursor(value: int) -> None:
    CURSOR_PATH.write_text(str(value))


def fetch_rows(conn: sqlite3.Connection, after_id: int) -> list[dict]:
    conn.row_factory = sqlite3.Row
    cur = conn.execute(
        """
        SELECT id, node_id, air_temperature, air_humidity, soil_moisture,
               soil_raw, rssi, snr, vbat, uptime, timestamp
        FROM measurement
        WHERE id > ?
        ORDER BY id
        LIMIT ?
        """,
        (after_id, BATCH_LIMIT),
    )
    rows = []
    for r in cur.fetchall():
        # timestamp у SQLite зберігається UTC-naive ("2026-08-24 13:44:04");
        # додаємо зону явно, щоб у хмарі не поплив час.
        ts = r["timestamp"]
        if ts and "+" not in ts and "Z" not in ts:
            ts = ts.replace(" ", "T") + "+00:00"
        rows.append({
            "source_id": r["id"],
            "node_id": r["node_id"],
            "air_t": r["air_temperature"],
            "air_h": r["air_humidity"],
            "soil": r["soil_moisture"],
            "soil_raw": r["soil_raw"],
            "rssi": r["rssi"],
            "snr": r["snr"],
            "vbat": r["vbat"],
            "uptime": r["uptime"],
            "ts": ts,
        })
    return rows


def labels_from_config() -> dict:
    """Мітки/пороги вузлів для фарбування в хмарі. Беремо з того ж джерела, що
    /greenhouses; якщо конфіг не читається — просто не шлемо (не критично)."""
    try:
        sys.path.insert(0, str(HERE))
        from thresholds import thresholds  # noqa: PLC0415 — залежить від sys.path

        out = {}
        for node_id, cfg in thresholds.greenhouses.items():
            out[node_id] = {
                "label": cfg.label,
                "thresholds": {
                    "air_t": [cfg.air_temperature.min, cfg.air_temperature.max],
                    "air_h": [cfg.air_humidity.min, cfg.air_humidity.max],
                    "soil": [cfg.soil_moisture.min, cfg.soil_moisture.max],
                },
            }
        return out
    except Exception as exc:  # noqa: BLE001 — мітки не критичні для даних
        print(f"# мітки не прочитались: {exc}")
        return {}


def post_batch(url: str, token: str, rows: list[dict], labels: dict | None) -> int:
    headers = {"Content-Type": "application/json"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    body = {"rows": rows}
    if labels:
        body["labels"] = labels
    r = httpx.post(url, json=body, headers=headers, timeout=30)
    r.raise_for_status()
    return r.json().get("written", 0)


def run_once(url: str, token: str, send_labels: bool) -> int:
    if not DB_PATH.exists():
        print(f"# нема {DB_PATH}")
        return 0
    after = read_cursor()
    with sqlite3.connect(f"file:{DB_PATH}?mode=ro", uri=True) as conn:
        rows = fetch_rows(conn, after)
    labels = labels_from_config() if send_labels else None
    if not rows and not labels:
        return 0
    written = post_batch(url, token, rows, labels)
    if rows:
        write_cursor(rows[-1]["source_id"])
        print(f"# надіслано {len(rows)} рядків (нових у хмарі {written}), "
              f"курсор → {rows[-1]['source_id']}")
    elif labels:
        print("# надіслано лише мітки")
    return len(rows)


def main() -> None:
    load_env()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--loop", action="store_true", help=f"демон, кожні {LOOP_SECONDS} с"
    )
    parser.add_argument("--url", default=os.environ.get("NODES_INGEST_URL", ""))
    parser.add_argument("--token", default=os.environ.get("NODES_TOKEN", ""))
    args = parser.parse_args()

    if not args.url:
        sys.exit("Потрібен NODES_INGEST_URL (у .env або --url)")

    if not args.loop:
        run_once(args.url, args.token, send_labels=True)
        return

    print(f"# forwarder: {DB_PATH} → {args.url}, кожні {LOOP_SECONDS} с")
    first = True
    while True:
        try:
            run_once(args.url, args.token, send_labels=first)
            first = False
        except Exception as exc:  # noqa: BLE001 — демон не має падати від одного збою
            print(f"# помилка циклу: {exc}")
        time.sleep(LOOP_SECONDS)


if __name__ == "__main__":
    main()
