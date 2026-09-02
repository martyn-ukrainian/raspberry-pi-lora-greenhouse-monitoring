#!/usr/bin/env python3
"""Локальний приймач пачок з плати: те, що робитиме /api/ingest, але на ноуті.

Навіщо, коли буде хмара:

- фаза 0 (шум від Wi-Fi) — радіо має реально передавати, а приймач у
  домашній мережі для цього досить; Vercel ще не потрібен;
- фаза 4 (звірка USB проти хмари) — пачки складаються назад у CSV з ТИМИ
  САМИМИ колонками, що друкує плата в Serial, тож `diff` двох файлів і є
  перевіркою, що тракт нічого не губить і не плутає.

Те, що в docs/довідка/INGEST-CONTRACT.md названо пастками, тут зроблено правильно,
щоб це був і приклад для серверного агента:
  - дедуплікація за (device, seq) — пачки приходять повторно й не по порядку;
  - відсутнє поле лишається порожнім, а не стає нулем;
  - `t` — секунди від старту прогону; час доби ставиться тут, при прийомі.

    uv run tools/ingest_sink.py --port 8008 --token abc123

Плата: -DINGEST_URL=\\"http://<IP ноута>:8008/api/ingest\\" (саме http).
"""

import argparse
import json
import socket
import sys
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

DEFAULT_OUT = Path(__file__).resolve().parent.parent / "data"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=8008)
    parser.add_argument("--token", default="",
                        help="очікуваний Bearer-токен; порожній = не перевіряти")
    parser.add_argument("--run", default="ingest", help="ім'я прогону для файлу")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    return parser.parse_args()


def lan_ip() -> str:
    """IP цього ноута в локальній мережі — те, що треба вписати в INGEST_URL."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))  # пакет не йде, лише вибір інтерфейсу
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


class Sink:
    """Один файл на прогін, дедуплікація за (device, seq), заголовок як у плати."""

    def __init__(self, out_dir: Path, run: str) -> None:
        out_dir.mkdir(parents=True, exist_ok=True)
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        self.path = out_dir / f"{run}-{stamp}.csv"
        self.file = self.path.open("a", encoding="utf-8")
        self.seen: set[tuple[str, int]] = set()
        self.labels: list[str] | None = None
        self.batches = 0
        self.dupes = 0

    def header(self, labels: list[str]) -> None:
        cols = ["elapsed_s", "water_ml", "event"]
        for label in labels:
            cols += [f"{label}_raw", f"{label}_mv"]
        cols += ["air_t", "air_h"]
        self.file.write(f"# ingest sink, старт {datetime.now().isoformat(timespec='seconds')}\n")
        self.file.write(",".join(cols) + "\n")

    def accept(self, batch: dict) -> str:
        key = (batch["device"], int(batch["seq"]))
        if key in self.seen:
            self.dupes += 1
            return "dup"
        self.seen.add(key)

        if self.labels is None:
            self.labels = list(batch["labels"])
            self.header(self.labels)

        for s in batch["samples"]:
            row = [f"{s['t']:.1f}", f"{s['water_ml']:.1f}", s.get("event", "")]
            for raw, mv in zip(s["raw"], s["mv"]):
                row += [str(raw), str(mv)]
            # Відсутнє поле лишається порожнім — так само, як у USB-CSV.
            row += [_fmt(s.get("air_t")), _fmt(s.get("air_h"))]
            self.file.write(",".join(row) + "\n")
        self.file.flush()
        self.batches += 1
        return "ok"


def _fmt(value) -> str:
    return "" if value is None else f"{value:.2f}"


def make_handler(sink: Sink, token: str):
    class Handler(BaseHTTPRequestHandler):
        def do_POST(self) -> None:  # noqa: N802 — ім'я диктує http.server
            if token and self.headers.get("Authorization") != f"Bearer {token}":
                self.send_response(401)
                self.end_headers()
                print(f"{_now()} 401 чужий токен")
                return

            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length)
            try:
                batch = json.loads(body)
                status = sink.accept(batch)
            except (ValueError, KeyError, TypeError) as exc:
                self.send_response(400)
                self.end_headers()
                print(f"{_now()} 400 зламане тіло: {exc}")
                return

            self.send_response(200)
            self.end_headers()
            n = len(batch["samples"])
            last = batch["samples"][-1] if n else {}
            print(f"{_now()} {status:3} seq={batch['seq']:<5} {n:2} замірів "
                  f"t={last.get('t', '?')} mv={last.get('mv')}  "
                  f"[пачок {sink.batches}, дублів {sink.dupes}]")

        def log_message(self, *_args) -> None:  # тиша замість стандартного логу
            pass

    return Handler


def _now() -> str:
    return datetime.now().strftime("%H:%M:%S")


def main() -> None:
    args = parse_args()
    sink = Sink(args.out, args.run)
    server = ThreadingHTTPServer(("0.0.0.0", args.port), make_handler(sink, args.token))
    print(f"слухаю http://{lan_ip()}:{args.port}/api/ingest  →  {sink.path}")
    print("у прошивку: -DINGEST_URL=\\\"http://%s:%d/api/ingest\\\"" % (lan_ip(), args.port))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print(f"\nзупинено: пачок {sink.batches}, дублів {sink.dupes}, файл {sink.path}")
        sys.exit(0)


if __name__ == "__main__":
    main()
