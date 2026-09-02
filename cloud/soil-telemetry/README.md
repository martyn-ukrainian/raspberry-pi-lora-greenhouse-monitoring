# soil-telemetry — приймач калібрувальної телеметрії

FastAPI на Vercel + Postgres у Neon. Приймає пачки з плати
(`firmware/soil-calibration`, середовище `wifi_field`) і показує їх на
сторінці з телефона. Формат пачки і пастки — `docs/довідка/INGEST-CONTRACT.md`;
навіщо це все — `docs/плани/план-wifi-телеметрії.md`.

```
main.py            — усі маршрути (FastAPI `app`; Vercel знаходить його сам)
public/index.html  — сторінка: значення, σ за хвилину, графік, «влив N мл», CSV
schema.sql         — таблиці; виконати один раз руками
requirements.txt   — fastapi, psycopg
vercel.json        — лише maxDuration
```

## Маршрути

| | Шлях | Токен | Що |
|---|---|---|---|
| POST | `/api/ingest` | так | пачка з плати; дубль `(device, seq)` → 200 без запису; у відповіді може бути `pour_ml` |
| POST | `/api/pour` | так | `{device, ml}` — кнопка «влив»; плата отримає при наступному POST |
| GET | `/api/devices` | — | плати, `last_seen`, `stale` |
| GET | `/api/live?device=` | — | останні 240 замірів, `sigma_mv` за 60 с по каналах, недоставлені доливи |
| GET | `/api/export.csv?device=&since=&until=` | — | CSV з колонками USB-логу + `received_at` |
| GET | `/api/health` | — | база відповідає? токен увімкнений? |
| GET | `/api/docs` | — | Swagger |

Токен — `Authorization: Bearer <INGEST_TOKEN>`. На запис обов'язковий;
читання відкрите (телеметрія калібрування, не секрети).

### Як ходить долив

Кнопка **не** пише воду в базу напряму. Вона кладе рядок у `pours`; плата
забирає його в полі `pour_ml` відповіді на свій наступний `/api/ingest`,
застосовує (`water_ml += ml`, подія `water+N` у наступному замірі), і коли
ця подія приїжджає в пачці — pour позначається доставленим. Доки не
доставлено, сервер повертає той самий `pour_ml` на кожен POST: загублена
відповідь не губить долив. Навіщо так складно: щоб USB-CSV і хмара показували
**однакову** воду, і фаза 4 могла їх порівняти `diff`-ом.

Прошивка має читати тіло відповіді — крок 1b.4 у `docs/плани/план-wifi-реалізація.md`.

## Розгортання (один раз, ~10 хв)

1. **Neon** → New project → у SQL Editor виконати `schema.sql`.
   Connection string брати з увімкненим **Pooled connection** (адреса з
   `-pooler`): функції короткоживучі, прямий ліміт з'єднань у Neon малий.
2. **Vercel** → Add New Project → цей репозиторій →
   **Root Directory: `cloud/soil-telemetry`** (монорепо; без цього Vercel
   побачить корінь і не знайде `main.py`). Framework: FastAPI (визначиться сам).
3. Environment Variables: `DATABASE_URL` (pooled-рядок з Neon),
   `INGEST_TOKEN` (будь-який довгий випадковий рядок: `openssl rand -hex 24`).
4. Deploy → `https://<проєкт>.vercel.app/api/health` має відповісти `{"ok":true,…}`.
5. Той самий токен і URL — у `firmware/.env`:
   `INGEST_URL=https://<проєкт>.vercel.app/api/ingest`, `INGEST_TOKEN=…`.

Сторінка — `https://<проєкт>.vercel.app/`. При першому натисканні «Влив» вона
спитає токен і запам'ятає його в браузері.

## Локально

```
cp .env.example .env         # DATABASE_URL, INGEST_TOKEN
uv venv && uv pip install -r requirements.txt uvicorn
uv run --env-file .env uvicorn main:app --reload --port 8010
```

Локальний Postgres замість Neon: `createdb soiltest && psql soiltest -f schema.sql`,
`DATABASE_URL=postgresql://localhost/soiltest`. Плата з `INGEST_URL=http://<IP
ноута>:8010/api/ingest` піде в нього так само, як у `tools/ingest_sink.py`.

## Звірка з USB (фаза 4)

`curl -o cloud.csv 'https://…/api/export.csv?device=<id>'` і порівняти з
USB-логом по `elapsed_s`: рядки, `water_ml`, `event`, усі `*_mv`. Остання
колонка `received_at` у USB відсутня — відкинути.
