# Сервер

`server/` — бекенд на Raspberry Pi: приймає виміри від шлюза, зберігає їх,
рахує пороги, шле сповіщення в Telegram і віддає дані застосунку.
Стек: Python 3.13, FastAPI, SQLModel + SQLite, Alembic, httpx, uv.

Складається з **трьох процесів**, кожен — окремий systemd-юніт на Pi:

| Процес | Файл | Роль |
|---|---|---|
| `agro-server` | `main.py` | HTTP API (порт 8008), алерти, запис у БД |
| `agro-adapter` | `usb_adapter.py` | читає NDJSON зі шлюза по USB → `POST /measurements`, `/events` |
| `agro-bot` | `telegram_bot.py` | Telegram-бот: команда `/get`, реєстр користувачів |

## API

| Метод | Шлях | Параметри | Повертає |
|---|---|---|---|
| GET | `/` | — | `{service, version}` |
| GET | `/health` | — | `{"status":"ok"}` — поки без перевірки БД |
| POST | `/measurements` | тіло `Measurement` | збережений запис; побічно запускає перевірку порогів |
| GET | `/measurements` | — | усі рядки (без ліміту — не для прод-трафіку) |
| GET | `/measurements/latest` | `node_id` | останній вимір або `null` |
| GET | `/measurements/aggregate` | `node_id`, `since` (деф. −4 год), `bucket_minutes` 1–1440 (деф. 5) | `[{bucket, count, air_temperature{min,max,avg}, air_humidity{…}, soil_moisture{…}}]` |
| GET | `/greenhouses` | — | `[{node_id, label, thresholds{…}}]` — з YAML, не з БД |
| GET | `/alerts` | `limit` ≤200 | збережені алерти, нові зверху |
| POST | `/alerts/ack-all` | — | `{"acknowledged": N}` |
| POST | `/events` | `{node_id, source, code, context?}` | подія заліза |
| GET | `/events` | `limit` ≤200 | події, нові зверху |

Swagger — `/docs`. CORS відкритий для всіх origin (застосунок ходить з LAN).
Агрегація написана на SQLite-функціях (`strftime`), на іншій БД без правок не
запрацює.

## Дані

| Таблиця | Поля | Навіщо |
|---|---|---|
| `measurement` | `node_id`, `air_temperature`, `air_humidity`, `soil_moisture`, `rssi?`, `snr?`, `vbat?`, `uptime?`, `timestamp` (UTC) | кожен пакет від вузла |
| `storedalert` | `node_id`, `label`, `sensor`, `kind` (high/low), `value`, `boundary`, `duration_minutes`, `created_at`, `acknowledged` | історія сповіщень для застосунку |
| `botuser` | `user_id` (Telegram), `chat_id`, `username`, `allow`, `first_seen`, `last_seen` | хто має доступ до бота |
| `nodeevent` | `node_id`, `source` (node/gateway), `code`, `name`, `context`, `timestamp` | brownout, watchdog, ресети тощо; зберігаються 14 днів |

Схема — через Alembic (5 міграцій: базова, rssi/snr, vbat/uptime, botuser,
nodeevent). `uv run alembic upgrade head` після кожного оновлення.

`node_id` у вузла числовий; адаптер перекладає його в текстову мітку через
`config/nodes.yaml`:

```yaml
nodes:
  0: greenhouse-1
  1: stage3-reference
  2: stage3-lowpower
  255: gateway
```

Невідомий номер — попередження в лог, пакет відкидається.

## Алерти

Пороги — `config/thresholds.yaml`, перевіряється pydantic при старті (невалідний
файл = сервер не підніметься):

```yaml
defaults:
  dwell_minutes: 5        # скільки тримати відхилення, перш ніж сигналити
  cooldown_minutes: 30    # пауза між повторними сповіщеннями
greenhouses:
  greenhouse-1:
    label: "Помідори 2 пльонки"
    air_temperature: {min: 20.0, max: 40.0, dwell_minutes: 3}
    air_humidity:    {min: 5.0,  max: 100.0, dwell_minutes: 10}
    soil_moisture:   {min: 5.0,  max: 100.0, dwell_minutes: 20}
```

Як працює: при кожному `POST /measurements` для кожного з трьох датчиків
значення класифікується як `high` / `low` / норма. Перше відхилення лише
запам'ятовується; сповіщення народжується, коли відхилення тримається
≥ `dwell_minutes` **і** від попереднього сповіщення минув `cooldown_minutes`.
`dwell` можна перекрити на датчик, `cooldown` — тільки глобальний. Час береться
з `timestamp` виміру, не з годинника сервера. Стан — у пам'яті процесу:
після рестарту «забувається», тобто відлік dwell починається знову.

Вологість повітря і ґрунту зараз навмисно розширені до 5–100, доки не заміряна
шкала ґрунту — інакше сипались би хибні алерти.

Текст сповіщення локалізується через `config/messages.yaml` (іконки, назви
датчиків, одиниці, «вище/нижче»):

```
🔥 Теплиця «Помідори 2 пльонки»
Температура повітря: 41.2°C - вище норми (поріг 40.0°C)
Триває вже 7 хв
```

Надсилає `notifiers/telegram.py` через Bot API (`sendMessage`, HTML); при
мережевій помилці — лише запис у лог, сервер не падає. `Notifier` — протокол
з одним методом `send(alert)`, щоб додати інший канал без правки логіки алертів.

## Telegram-бот

Окремий процес, long polling. Одна команда — **`/get`**: для кожної теплиці з
`/greenhouses` бере `/measurements/latest` і відповідає:

```
🏠 Помідори 2 пльонки
🌡 27.4°C  💧 71%  🌱 43%
```

Доступ: адмін (чат із `TELEGRAM_CHAT_ID`) або користувач із `allow=true` у
таблиці `botuser`. Будь-яке повідомлення реєструє користувача (без доступу);
дозвіл видається вручну (`BotUserRepository.set_allow`, див. `docs/довідка/pi-cheatsheet.md`).
Чужим бот відповідає «⛔️ Немає доступу».

## Конфіг (`.env`)

| Змінна | Дефолт | Що |
|---|---|---|
| `DATABASE_URL` | `sqlite:///agro.db` | БД |
| `TELEGRAM_TOKEN` | **обов'язкова** | токен бота від @BotFather |
| `TELEGRAM_CHAT_ID` | **обов'язкова** | чат оператора/адміна |
| `SERVER_URL` | `http://127.0.0.1:8000` | куди адаптер і бот ходять по HTTP — на практиці `:8008` |
| `SERIAL_PORT` | `/tmp/agro_adapter` | USB-порт шлюза; на Pi `/dev/ttyUSB0` |
| `BAUDRATE` | `115200` | |
| `GATEWAY_PORT` | `/tmp/agro_gateway` | лише для симулятора (socat-пара) |

## Запуск

```bash
cd server && uv sync
cp .env.example .env                       # заповнити TELEGRAM_*
uv run alembic upgrade head
uv run fastapi dev main.py --host 0.0.0.0 --port 8008
uv run python usb_adapter.py               # окремий термінал
uv run python telegram_bot.py              # окремий термінал
```

Без заліза: `uv run python simulate.py` (HTTP-симулятор, 3 теплиці) або
`gateway_simulator.py` + socat (імітує NDJSON шлюза, включно з бітмасками подій).

Тести: `uv run pytest` — алерти (dwell/cooldown) і події. Лінт: ruff через
pre-commit. Логи: консоль + `server/logs/agro.log` з добовою ротацією (14 днів);
на Pi — `journalctl -u agro-server -f`.

Деплой і обслуговування на Pi — [08-operations.md](08-operations.md).

## Відомі обмеження (стан на 2026-08-23)

- `/health` не перевіряє БД; нема алерту «Pi мовчить» і «вузол мовчить».
- Адаптер не перепідключається до USB — покладається на restart від systemd.
- Стан dwell/cooldown не переживає рестарт.
- Нема автентифікації API; розраховано на ізольовану LAN.
- Нема бекапу БД, WAL, індексів, ретенції вимірів.
- Сконфігурована одна теплиця; для інших `node_id` алерти мовчки не працюють.
- `NOTIFIER=telegram_hub` з README — задум, у коді є лише прямий Telegram.
