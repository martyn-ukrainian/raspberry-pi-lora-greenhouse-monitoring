# Експлуатація

Що робити з працюючою системою: зайти на Pi, подивитись, оновити, полагодити.
Детальна шпаргалка — `docs/довідка/pi-cheatsheet.md`; тут — найчастіше.

## Доступ до Pi

```bash
ssh agro@agro-pi.local              # або scripts/pi.sh (alias `pi`), ключ через ssh-copy-id
arp -a | grep -i raspberry          # якщо .local не резолвиться — знайти IP
```

## Сервіси

| Юніт | Що | Примітка |
|---|---|---|
| `agro-server` | FastAPI :8008 | |
| `agro-adapter` | USB шлюз → сервер | прив'язаний до `/dev/ttyUSB0`, стартує коли шлюз воткнуто |
| `agro-bot` | Telegram `/get` | |
| `agro-simulator` | тестові дані | **вимкнено** — реальні датчики |

```bash
sudo systemctl status agro-server agro-adapter agro-bot
sudo systemctl restart agro-server
journalctl -u agro-server -f                 # логи, Ctrl+C виходить, сервіс живе
journalctl -u agro-adapter --since "1 hour ago"
curl http://agro-pi.local:8008/health        # {"status":"ok"}
```

## Оновити код

```bash
cd ~/agro && git pull
cd server && uv sync && uv run alembic upgrade head
sudo systemctl restart agro-server agro-bot agro-adapter
```

Шлюз прошивається з ноутбука командою `make deploy-gateway` — вона сама
зупиняє й запускає `agro-adapter` (див. [04-firmware.md](04-firmware.md#збірка-й-заливка)).

## Дати доступ до бота

Хто написав боту — потрапив у `botuser` з `allow=0`:

```bash
cd ~/agro/server
uv run python -c "from bot_users import BotUserRepository; from database import engine; \
  [print(u.user_id, u.username, u.allow) for u in BotUserRepository(engine).list_all()]"
uv run python -c "from bot_users import BotUserRepository; from database import engine; \
  BotUserRepository(engine).set_allow(<USER_ID>, True)"
```

## База

```bash
sqlite3 ~/agro/server/agro.db "SELECT COUNT(*) FROM measurement;"
sqlite3 ~/agro/server/agro.db "SELECT * FROM storedalert ORDER BY created_at DESC LIMIT 5;"
sqlite3 ~/agro/server/agro.db "SELECT * FROM nodeevent ORDER BY timestamp DESC LIMIT 10;"
sqlite3 ~/agro/server/agro.db "DELETE FROM measurement WHERE timestamp < datetime('now','-7 days'); VACUUM;"
```

Бекапу поки нема — це перший пункт у боргах нижче.

## Коли щось мовчить

| Симптом | Де дивитись |
|---|---|
| Нема нових вимірів | `journalctl -u agro-adapter` — чи бачить порт; OLED шлюза — чи приймає LoRa; `nodeevent` — чи вузол ресетиться (`boot` скинувся в 1) |
| Шлюз приймає, сервер не пише | `journalctl -u agro-server`; невідомий `node_id` → додати в `config/nodes.yaml` |
| Алерти не приходять | теплиця є в `thresholds.yaml`? dwell ще не минув? cooldown? `TELEGRAM_*` у `.env`? |
| Бот не відповідає | `agro-bot` живий? користувач має `allow=1`? |
| Вузол мовчить у полі | `vbat` в останньому пакеті; події `battery_low/critical`, `lora_tx_failed` |

## Вимкнення

`sudo shutdown -h now` перед відключенням живлення — інакше ризик для SD-карти.
Ребут — ~1 хв, сервіси піднімаються самі.

## Борги експлуатації (пріоритет згори вниз)

1. Бекап `agro.db` (cron + копія на інший носій), WAL-режим, ретенція.
2. Алерт «Pi мовчить» і «вузол мовчить» — зараз тишу ніхто не помітить.
3. UPS / захищене живлення Pi.
4. Справжній `/health` з перевіркою БД і свіжості даних.
5. `deploy.sh` замість ручної послідовності; CI з тестами.
6. API-ключ, доступ лише по ключах SSH.
