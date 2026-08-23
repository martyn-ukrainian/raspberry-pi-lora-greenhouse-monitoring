-- Схема приймача калібрувальної телеметрії. Застосовується один раз руками:
--   Neon → SQL Editor → вставити й виконати
--   локально: psql "$DATABASE_URL" -f schema.sql
--
-- Джерело правди про формат — docs/INGEST-CONTRACT.md. Тут лише те, що
-- потрібно, щоб не втратити дані з трьох пасток контракту:
--   1. відсутнє поле ≠ нуль            → air_t/air_h/event NULLable, без DEFAULT 0
--   2. t — секунди від старту прогону  → received_at ставить сервер
--   3. пачки приходять повторно         → PRIMARY KEY (device, seq) у batches

CREATE TABLE IF NOT EXISTS devices (
    id          text PRIMARY KEY,          -- MAC плати без роздільників
    label       text,                      -- людська назва, правиться руками
    labels      text[] NOT NULL DEFAULT '{}', -- імена каналів з останньої пачки
    first_seen  timestamptz NOT NULL DEFAULT now(),
    last_seen   timestamptz NOT NULL DEFAULT now()
);

-- Одна пачка = один POST. Сам факт запису тут і є дедуплікацією: другий
-- INSERT тієї самої (device, seq) впирається в ключ, і samples не пишуться.
CREATE TABLE IF NOT EXISTS batches (
    device      text NOT NULL REFERENCES devices(id),
    seq         integer NOT NULL,
    n           integer NOT NULL,
    received_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (device, seq)
);

-- raw/mv — масиви, а не 8 колонок: кількість каналів задає плата, а не схема.
CREATE TABLE IF NOT EXISTS samples (
    id          bigserial PRIMARY KEY,
    device      text NOT NULL REFERENCES devices(id),
    seq         integer NOT NULL,
    t           real NOT NULL,              -- elapsed_s на платі
    water_ml    real NOT NULL,
    event       text,                       -- NULL = нічого не сталось
    raw         integer[] NOT NULL,
    mv          integer[] NOT NULL,
    air_t       real,                       -- NULL = SHT31 не відповів
    air_h       real,
    received_at timestamptz NOT NULL
);

-- Батарея: є лише на акумуляторі, на USB поля не надсилаються → NULL.
-- ALTER … IF NOT EXISTS, бо таблиця могла бути створена до появи полів.
ALTER TABLE samples ADD COLUMN IF NOT EXISTS vbat    real;
ALTER TABLE samples ADD COLUMN IF NOT EXISTS bat_pct smallint;

-- Сторінка читає «останні N по платі», експорт — діапазон за часом.
CREATE INDEX IF NOT EXISTS samples_device_received_idx
    ON samples (device, received_at);

-- Кнопка «влив N мл» зі сторінки. Запис іде ЧЕРЕЗ плату, а не повз неї:
-- плата забирає pour у відповіді на свій наступний POST, застосовує
-- (water_ml += ml, подія water+N у наступному замірі), і коли ця подія
-- приїжджає в пачці — pour вважається доставленим. Так USB-CSV і хмара
-- показують однакову воду, і фаза 4 їх може порівняти.
CREATE TABLE IF NOT EXISTS pours (
    id           serial PRIMARY KEY,
    device       text NOT NULL REFERENCES devices(id),
    ml           real NOT NULL,
    created_at   timestamptz NOT NULL DEFAULT now(),
    sent_at      timestamptz,               -- коли віддали платі у відповіді
    delivered_at timestamptz                -- коли побачили water+N у даних
);

CREATE INDEX IF NOT EXISTS pours_pending_idx
    ON pours (device, created_at) WHERE delivered_at IS NULL;
