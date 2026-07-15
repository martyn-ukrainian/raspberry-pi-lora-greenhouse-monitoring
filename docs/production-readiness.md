# Оцінка production-readiness

Знято: 2026-07-15. Стан після setup Pi 5 + systemd + деплой першої версії
(до підключення реальних LoRa-модулів і сенсорів).

**Загалом: 6.5/10** для solo-оператора (домашня теплиця). ~40% для
комерціалізації.

Легенда пріоритетів:
- 🔴 **Блокер** — за 3-4 дні "install and forget" не вийде без цього.
- 🟡 **Важливо** — робити перед реальним експлуатуванням на місяці.
- 🟢 **Nice-to-have** — покращення якості, не критично для домашнього MVP.

---

## 1. Безпека — 4/10

**Що є:**
- Backend не root (systemd `User=agro`).
- SSH-ключі можна вмикнути.
- Telegram bot ізольований від решти системи.

**Що не так:**
- 🔴 **API без автентифікації.** Будь-хто у WiFi-мережі може POST-нути
  фейковий вимір і спровокувати алерт. У домашній теплиці не критично,
  але дірка при будь-якій експозиції в інтернет.
- 🟡 **CORS `allow_origins=["*"]`** — будь-який сайт з будь-якого домену
  може стукати в API.
- 🟡 **HTTP, не HTTPS.** На LAN ок, для інтернету треба сертифікат
  (Caddy + Let's Encrypt автоматично).
- 🟡 **Telegram-токен у plain-text `.env`.** Компроміс Pi = компроміс бота.
- 🟢 **SSH з паролем ще увімкнений** — треба перейти на key-only.

## 2. Надійність — 6/10

**Що є:**
- systemd auto-restart на крашу.
- SQLite ACID.
- Anti-spam для алертів (dwell + cooldown).

**Що не так:**
- 🔴 **Alert state (`_state`) в памʼяті.** Рестарт сервера → забуто хто
  видав алерт коли. Може прийти дублікат (cooldown обнулиться) або
  пропущений (dwell не встиг натекти).
- 🟡 **Telegram у критичному шляху.** POST /measurements синхронно чекає
  Telegram-response. Повільний TG → повільний POST → LoRa-адаптер таймаутиться.
- 🟡 **Немає retry для Telegram.** Один timeout → алерт втрачено назавжди.
- 🟡 **Немає graceful shutdown.** `systemctl restart` уриває запити на льоту.
- 🟢 **`/health` тільки повертає OK.** Не перевіряє DB, Telegram, диск.

## 3. Data integrity — 5/10

**Що є:**
- SQLite ACID, UTC серіалізація на бекенді.
- Alembic налаштований (хоча не використовується для реальних міграцій).

**Що не так:**
- 🔴 **Немає бекапів.** SD-карта на Pi — failure point у спекотних умовах
  (теплиця +30°C). За рік-два накриється, з нею вся історія вимірів.
- 🔴 **DB росте вічно.** ~17k рядків/добу (симулятор). За рік — 6 млн,
  за 3 роки — SD-карта заповниться.
- 🟡 **SQLite не в WAL-режимі.** Дефолт `journal` менш crash-safe при
  перебоях живлення.
- 🟡 **Немає unique constraint** — LoRa retry → дублікати в БД.

## 4. Observability — 4/10

**Що є:**
- Logs з ротацією (`agro.log`, 14 днів).
- systemd journal ловить все.

**Що не так:**
- 🔴 **Немає моніторингу самого Pi.** Pi офлайн → оператор дізнається
  тільки коли зайде в апку. Ніяких "Pi не відповідає N хвилин".
- 🟡 **Немає metrics.** "Скільки алертів за тиждень?", "яка середня latency?",
  "коли останній раз рестартувалось?" — нема відповіді без ручного `grep`.
- 🟢 **Немає dashboard.** Grafana + Prometheus стандарт, для 1 оператора overkill.

## 5. Scalability — 8/10

**Поточне навантаження:** ~17k рядків/добу, ~50MB/місяць. Pi 5 2GB жує без напруження.

**Що не так:**
- 🟡 **Немає індексів** на `measurement.timestamp` і `.node_id`.
  Aggregate-endpoint робить `WHERE node_id=... AND timestamp >= ...` —
  сканує повну таблицю. При 100k+ рядків стане повільно (>500ms).
- 🟢 **Aggregate може стати дорогим** при роках даних. Треба буде або
  матеріалізовані таблиці, або TimescaleDB.

## 6. Тести — 3/10

**Що є:**
- Тестова папка з pytest.

**Що не так:**
- 🔴 **Coverage мінімальне** (треба перевірити реально).
- 🟡 **Немає integration-тестів** — HTTP endpoint-и не тестуються end-to-end.
- 🟡 **CI не налаштовано** — тести не бігають при push.

## 7. Deployment — 7/10

**Що є:**
- systemd авто-старт.
- Cheatsheet + SSH-shortcut.

**Що не так:**
- 🟡 **Deploy manual.** `git pull → uv sync → alembic upgrade → systemctl restart`
  — 4 команди. Легко забути `uv sync` при нових depends.
- 🟡 **Немає rollback.** Пушнув баг → сервер впав → лізти SSH-ом і `git reset --hard`.
- 🟡 **Немає zero-downtime.** `systemctl restart` = ~5 сек downtime.

## 8. Networking — 5/10

**Що є:**
- Працює на LAN через `agro-pi.local`.

**Що не так:**
- 🔴 **Тільки з домашньої WiFi.** Оператор в кафе — Pi недосяжний.
- 🟡 **Router = SPOF.** Ліг роутер — Pi недосяжний.
- 🟢 **Немає fallback WiFi** (наприклад phone-hotspot).

## 9. Hardware — 5/10

**Що не так:**
- 🔴 **Один Pi = SPOF.** Fail → все стало.
- 🔴 **SD-карта — reliability проблема.** Cards fail в спекотних умовах.
  Треба перехід на USB-SSD через `USB Boot`, або мінімум `high endurance`
  MicroSD.
- 🔴 **Немає UPS.** Відключення світла = раптова смерть Pi. SD-карти
  особливо не люблять несподіваного power-off.
- 🟡 **Немає корпусу.** У теплиці волога, пил, комахи. Треба IP54+
  з пасивним охолодженням.

## 10. Документація — 6/10

**Що є:**
- `docs/pi-cheatsheet.md` — Pi-команди.
- `README-UA.md`, `docs/alerts.md`.

**Що не так:**
- 🟡 **Немає architecture-діаграми.**
- 🟡 **Немає incident runbook** — "що робити коли Pi мертвий".
- 🟡 **Немає onboarding-документа** для нового devs.

---

## Roadmap до "install and forget" (3-4 дні)

### День 1 — Persistence + Backup
1. **Перенести DB на USB-SSD** — SD-карта надто ненадійна.
2. **Автоматичний backup БД** — `sqlite3 .backup` кожні 6 год на другий USB
   (або rsync на Mac).
3. **WAL mode** для SQLite (`PRAGMA journal_mode=WAL`).
4. **Retention job** — cron видаляє виміри старші 90 днів.

### День 2 — Observability + Alerting
1. **Health check реальний.** `/health` перевіряє DB (SELECT 1),
   Telegram-connect, вільне місце на диску.
2. **Pi liveness alert.** Окремий бот або cloud-cron стукає в `/health`
   кожні 5 хв → нема відповіді → нотифікація.
3. **Індекси на БД** — 2 рядки CREATE INDEX, latency впаде в 10x.
4. **Async Telegram send** — не блокує POST /measurements.

### День 3 — Deploy + Security
1. **Deploy-скрипт** `scripts/deploy.sh` — одна команда:
   `ssh pi 'cd ~/agro && git pull && cd server && uv sync && uv run alembic upgrade head && sudo systemctl restart agro-server'`
2. **API-token auth** — простий header `X-API-Key`. Клієнт і adapter шлють токен.
3. **SSH тільки key-auth** — вимкнути password.
4. **Tailscale** — доступ з-за меж дому + Pi liveness через mesh.

### День 4 — Тести + Docs
1. **Integration-тести** для endpoint-ів (pytest + httpx.TestClient).
2. **CI на GitHub Actions** — тести бігають при push.
3. **Architecture-doc** — 1 сторінка з діаграмою.

## Що можна відкласти

- Redis / message queue — overkill для трьох теплиць.
- Kubernetes / Docker — теж.
- Prometheus + Grafana — nice-to-have, не критично.
- WebSocket real-time — до кращих часів.
- Multi-user, multi-tenant — не потрібно, один оператор.

## Найкритичніші 3 речі перед LoRa+сенсорами

1. **Backup БД** — SD-карта помре, це питання часу.
2. **UPS для Pi** — світло відключиться, це питання коли.
3. **Alert якщо Pi мертвий** — без цього вся система = false confidence
   (оператор думає що все ок, а Pi мовчки лежить дві доби).

Робити ці три **перед** розпайкою LoRa. Все інше — після.

---

## Оцінка для різних сценаріїв

**Домашня теплиця батька (ти поруч, підправиш):**
- **90% готово.** Найбільший ризик — SD-карта здохне через рік. USB-SSD
  + backup-cron → 95%. Ліміт-фактор далі — тільки залізо.

**Продаж 10-20 фермерам у сусідньому селі:**
- **60% готово.** Треба: token auth, remote-support, deploy-скрипт,
  liveness-monitoring. 2-3 тижні роботи.

**Комерційний продукт (сотні клієнтів):**
- **40% готово.** Треба: multi-tenant, HTTPS, ролі, SLA, підтримка,
  onboarding UX, білінг. 3-4 місяці роботи мінімум.
