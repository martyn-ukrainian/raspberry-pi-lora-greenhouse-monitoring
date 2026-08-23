"""Приймач калібрувальної телеметрії: /api/ingest і те, що потрібно сторінці.

Vercel підхоплює FastAPI-застосунок з іменем `app` у main.py сам, без
vercel.json-магії. Сховище — Postgres (Neon), DATABASE_URL у змінних
середовища. Функція живе коротко, тож з'єднання відкривається на запит і
закривається — без пулу всередині процесу; пулом займається Neon
(pooled-рядок у DATABASE_URL).

Формат пачки — docs/INGEST-CONTRACT.md. Три пастки звідти обробляються тут
явно, кожна з коментарем на місці.

Локально:
    cp .env.example .env   # DATABASE_URL, INGEST_TOKEN
    uv run --env-file .env uvicorn main:app --reload --port 8010
"""

from __future__ import annotations

import csv
import io
import os
import statistics
from datetime import datetime, timezone
from typing import Annotated

import psycopg
from fastapi import Depends, FastAPI, Header, HTTPException, Query, Response
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field

app = FastAPI(title="agro soil telemetry", docs_url="/api/docs", openapi_url="/api/openapi.json")

DATABASE_URL = os.environ.get("DATABASE_URL", "")
INGEST_TOKEN = os.environ.get("INGEST_TOKEN", "")

# Скільки секунд без пачок означає «плата мовчить». Пачка раз на 30 с плюс
# переконнект — 90 с дає два пропущені вікна, не більше.
STALE_AFTER_S = 90
SIGMA_WINDOW_S = 60


# --------------------------------------------------------------------------
# Моделі контракту
# --------------------------------------------------------------------------


class Sample(BaseModel):
    t: float
    water_ml: float
    event: str | None = None          # відсутнє = нічого не сталось, НЕ ""
    raw: list[int]
    mv: list[int]
    air_t: float | None = None        # відсутнє = сенсора нема, НЕ 0
    air_h: float | None = None


class Batch(BaseModel):
    device: str = Field(min_length=4, max_length=32)
    seq: int = Field(ge=0)
    labels: list[str] = Field(min_length=1, max_length=16)
    samples: list[Sample] = Field(max_length=1000)


class Pour(BaseModel):
    device: str
    ml: float = Field(gt=0, le=5000)


# --------------------------------------------------------------------------
# Інфраструктура
# --------------------------------------------------------------------------


def db() -> psycopg.Connection:
    if not DATABASE_URL:
        raise HTTPException(500, "DATABASE_URL не задано")
    return psycopg.connect(DATABASE_URL)


def require_token(authorization: Annotated[str | None, Header()] = None) -> None:
    """Спільний токен у заголовку. Порожній INGEST_TOKEN = перевірка вимкнена —
    це лише для локального запуску; на Vercel токен обов'язковий."""
    if not INGEST_TOKEN:
        return
    if authorization != f"Bearer {INGEST_TOKEN}":
        raise HTTPException(401, "чужий токен")


Auth = Depends(require_token)


# --------------------------------------------------------------------------
# /api/ingest
# --------------------------------------------------------------------------


@app.post("/api/ingest", dependencies=[Auth])
def ingest(batch: Batch) -> dict:
    now = datetime.now(timezone.utc)
    n_ch = len(batch.labels)
    for s in batch.samples:
        if len(s.raw) != n_ch or len(s.mv) != n_ch:
            raise HTTPException(422, "довжина raw/mv не збігається з labels")

    with db() as conn, conn.cursor() as cur:
        cur.execute(
            """
            INSERT INTO devices (id, labels, first_seen, last_seen)
            VALUES (%s, %s, %s, %s)
            ON CONFLICT (id) DO UPDATE SET labels = EXCLUDED.labels, last_seen = EXCLUDED.last_seen
            """,
            (batch.device, batch.labels, now, now),
        )

        # Пастка 3: пачки приходять повторно й не по порядку. Ключ (device, seq)
        # робить повтор безпечним: INSERT нічого не поверне — і ми НЕ пишемо
        # samples вдруге, але відповідаємо 200, щоб плата не крутила чергу.
        cur.execute(
            """
            INSERT INTO batches (device, seq, n, received_at)
            VALUES (%s, %s, %s, %s)
            ON CONFLICT DO NOTHING
            RETURNING seq
            """,
            (batch.device, batch.seq, len(batch.samples), now),
        )
        fresh = cur.fetchone() is not None

        if fresh and batch.samples:
            cur.executemany(
                """
                INSERT INTO samples
                    (device, seq, t, water_ml, event, raw, mv, air_t, air_h, received_at)
                VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s)
                """,
                [
                    # Пастка 1: None лишається NULL, жодних `or 0`.
                    (batch.device, batch.seq, s.t, s.water_ml, s.event,
                     s.raw, s.mv, s.air_t, s.air_h, now)
                    for s in batch.samples
                ],
            )
            # Підтвердження доливів: плата застосувала pour і прислала water+N.
            for s in batch.samples:
                if s.event and s.event.startswith("water+"):
                    _ack_pour(cur, batch.device, s.event, now)

        pour_ml = _next_pour(cur, batch.device, now)
        conn.commit()

    reply: dict = {"ok": True, "dup": not fresh}
    if pour_ml is not None:
        reply["pour_ml"] = pour_ml
    return reply


def _ack_pour(cur: psycopg.Cursor, device: str, event: str, now: datetime) -> None:
    try:
        ml = float(event[len("water+"):])
    except ValueError:
        return
    # Найстаріший недоставлений з тим самим об'ємом. Долив через USB-команду з
    # таким самим числом теж його «закриє» — прийнятно: і там, і там вода влита.
    cur.execute(
        """
        UPDATE pours SET delivered_at = %s
        WHERE id = (
            SELECT id FROM pours
            WHERE device = %s AND delivered_at IS NULL AND abs(ml - %s) < 0.05
            ORDER BY created_at LIMIT 1
        )
        """,
        (now, device, ml),
    )


def _next_pour(cur: psycopg.Cursor, device: str, now: datetime) -> float | None:
    """Найстаріший недоставлений pour. Віддаємо його КОЖНОГО разу, доки плата
    не підтвердить подією: якщо відповідь загубилась, плата повторить POST і
    отримає той самий pour знову — це краще, ніж мовчки втратити долив.
    Зворотний ризик (плата застосувала, перезавантажилась до наступної пачки,
    отримала вдруге) рідкісний і видно в даних як два water+N поспіль."""
    cur.execute(
        """
        UPDATE pours SET sent_at = COALESCE(sent_at, %s)
        WHERE id = (
            SELECT id FROM pours
            WHERE device = %s AND delivered_at IS NULL
            ORDER BY created_at LIMIT 1
        )
        RETURNING ml
        """,
        (now, device),
    )
    row = cur.fetchone()
    return float(row[0]) if row else None


# --------------------------------------------------------------------------
# Читання для сторінки
# --------------------------------------------------------------------------


@app.get("/api/devices")
def devices() -> list[dict]:
    with db() as conn, conn.cursor() as cur:
        cur.execute(
            "SELECT id, label, labels, first_seen, last_seen FROM devices ORDER BY last_seen DESC"
        )
        rows = cur.fetchall()
    now = datetime.now(timezone.utc)
    return [
        {
            "id": r[0],
            "label": r[1],
            "labels": r[2],
            "first_seen": r[3].isoformat(),
            "last_seen": r[4].isoformat(),
            "stale": (now - r[4]).total_seconds() > STALE_AFTER_S,
        }
        for r in rows
    ]


@app.get("/api/live")
def live(device: str, n: int = Query(240, ge=1, le=5000)) -> dict:
    """Останні n замірів, σ за хвилину по каналах і стан «мовчить / ні»."""
    with db() as conn, conn.cursor() as cur:
        cur.execute("SELECT labels, last_seen FROM devices WHERE id = %s", (device,))
        dev = cur.fetchone()
        if not dev:
            raise HTTPException(404, "невідома плата")
        labels, last_seen = dev
        cur.execute(
            """
            SELECT t, water_ml, event, raw, mv, air_t, air_h, received_at, seq
            FROM samples WHERE device = %s
            ORDER BY received_at DESC, seq DESC, t DESC
            LIMIT %s
            """,
            (device, n),
        )
        rows = list(reversed(cur.fetchall()))
        cur.execute(
            "SELECT id, ml, created_at, sent_at FROM pours "
            "WHERE device = %s AND delivered_at IS NULL ORDER BY created_at",
            (device,),
        )
        pending = [
            {"id": p[0], "ml": p[1], "created_at": p[2].isoformat(),
             "sent": p[3] is not None}
            for p in cur.fetchall()
        ]

    now = datetime.now(timezone.utc)
    samples = [
        {"t": r[0], "water_ml": r[1], "event": r[2], "raw": r[3], "mv": r[4],
         "air_t": r[5], "air_h": r[6], "received_at": r[7].isoformat(), "seq": r[8]}
        for r in rows
    ]
    return {
        "device": device,
        "labels": labels,
        "last_seen": last_seen.isoformat(),
        "stale": (now - last_seen).total_seconds() > STALE_AFTER_S,
        "sigma_mv": sigma_by_channel(rows, len(labels)),
        "pending_pours": pending,
        "samples": samples,
    }


def sigma_by_channel(rows: list[tuple], n_ch: int) -> list[float | None]:
    """σ у мВ за останню хвилину за годинником ПЛАТИ (t), не за received_at:
    пачка приїжджає цілою, і за часом прийому всі 30 замірів — одна мить."""
    if not rows:
        return [None] * n_ch
    t_last = rows[-1][0]
    window = [r for r in rows if r[0] >= t_last - SIGMA_WINDOW_S]
    out: list[float | None] = []
    for ch in range(n_ch):
        vals = [r[4][ch] for r in window if len(r[4]) > ch]
        out.append(round(statistics.pstdev(vals), 2) if len(vals) >= 3 else None)
    return out


@app.get("/api/export.csv")
def export_csv(
    device: str,
    since: datetime | None = None,
    until: datetime | None = None,
) -> Response:
    """CSV рівно з колонками USB-логу — щоб `diff` з ним і був звіркою фази 4.
    Додатково остання колонка received_at: її в USB нема, при порівнянні
    відкидати."""
    with db() as conn, conn.cursor() as cur:
        cur.execute("SELECT labels FROM devices WHERE id = %s", (device,))
        dev = cur.fetchone()
        if not dev:
            raise HTTPException(404, "невідома плата")
        labels = dev[0]
        cur.execute(
            """
            SELECT t, water_ml, event, raw, mv, air_t, air_h, received_at
            FROM samples
            WHERE device = %s
              AND (%s::timestamptz IS NULL OR received_at >= %s)
              AND (%s::timestamptz IS NULL OR received_at <= %s)
            ORDER BY received_at, seq, t
            """,
            (device, since, since, until, until),
        )
        rows = cur.fetchall()

    buf = io.StringIO()
    buf.write(f"# agro soil telemetry export, device {device}, "
              f"{datetime.now(timezone.utc).isoformat(timespec='seconds')}\n")
    header = ["elapsed_s", "water_ml", "event"]
    for label in labels:
        header += [f"{label}_raw", f"{label}_mv"]
    header += ["air_t", "air_h", "received_at"]
    w = csv.writer(buf, lineterminator="\n")
    w.writerow(header)
    for t, water, event, raw, mv, air_t, air_h, received in rows:
        row = [f"{t:.1f}", f"{water:.1f}", event or ""]
        for r, m in zip(raw, mv):
            row += [r, m]
        # Пастка 1 у зворотний бік: NULL → порожня клітинка, як у USB-CSV.
        row += ["" if air_t is None else f"{air_t:.2f}",
                "" if air_h is None else f"{air_h:.2f}",
                received.isoformat(timespec="seconds")]
        w.writerow(row)

    return Response(
        buf.getvalue(),
        media_type="text/csv; charset=utf-8",
        headers={"Content-Disposition": f'attachment; filename="{device}.csv"'},
    )


# --------------------------------------------------------------------------
# Кнопка «влив N мл»
# --------------------------------------------------------------------------


@app.post("/api/pour", dependencies=[Auth])
def pour(p: Pour) -> dict:
    with db() as conn, conn.cursor() as cur:
        cur.execute("SELECT 1 FROM devices WHERE id = %s", (p.device,))
        if not cur.fetchone():
            raise HTTPException(404, "невідома плата")
        cur.execute(
            "INSERT INTO pours (device, ml) VALUES (%s, %s) RETURNING id, created_at",
            (p.device, p.ml),
        )
        pid, created = cur.fetchone()
        conn.commit()
    return {"ok": True, "id": pid, "created_at": created.isoformat()}


@app.get("/api/health")
def health() -> JSONResponse:
    try:
        with db() as conn, conn.cursor() as cur:
            cur.execute("SELECT count(*) FROM batches")
            batches = cur.fetchone()[0]
    except Exception as exc:  # noqa: BLE001 — health має відповісти, не впасти
        return JSONResponse({"ok": False, "error": str(exc)}, status_code=503)
    return JSONResponse({"ok": True, "batches": batches, "token_required": bool(INGEST_TOKEN)})


# Локально uvicorn має віддавати сторінку сам; на Vercel public/ роздається
# статично ще до функції, тож там цей mount просто не отримує запитів.
# Маршрути /api/* оголошені вище і мають пріоритет над mount.
_PUBLIC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "public")
if os.path.isdir(_PUBLIC):
    from fastapi.staticfiles import StaticFiles

    app.mount("/", StaticFiles(directory=_PUBLIC, html=True), name="public")
