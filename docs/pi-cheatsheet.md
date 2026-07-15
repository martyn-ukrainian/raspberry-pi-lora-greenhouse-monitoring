# Raspberry Pi cheatsheet

Раз-у-місяць-команди щоб не гуглити.

## SSH

```bash
ssh agro@agro-pi.local
```

Якщо `.local` не резолвиться — знайти IP:
```bash
# на Mac:
arp -a | grep -i raspberry
# або в адмінці роутера
```

Потім:
```bash
ssh agro@192.168.0.X
```

## Статус сервісів

```bash
sudo systemctl status agro-server agro-simulator
```

Обидва мають бути `active (running)`.

## Логи

```bash
# сервер, follow-режим (як tail -f):
journalctl -u agro-server -f

# simulator:
journalctl -u agro-simulator -f

# останні 100 рядків без follow:
journalctl -u agro-server -n 100

# логи з певної дати:
journalctl -u agro-server --since "1 hour ago"
journalctl -u agro-server --since "2026-07-15 10:00"
```

`Ctrl+C` виходить з follow — сервіс продовжує жити.

## Рестарт сервісів

```bash
sudo systemctl restart agro-server
sudo systemctl restart agro-simulator
```

Або зупинити/стартнути:
```bash
sudo systemctl stop agro-server
sudo systemctl start agro-server
```

## Deploy нових змін

Після `git push` з Mac:

```bash
cd ~/agro
git pull
cd server
uv sync                          # якщо є нові deps
uv run alembic upgrade head      # якщо є нові міграції
sudo systemctl restart agro-server agro-simulator
```

## Вимкнути автостарт (тимчасово)

```bash
sudo systemctl disable agro-server
sudo systemctl disable agro-simulator
```

Увімкнути назад:
```bash
sudo systemctl enable agro-server agro-simulator
```

## Ребут

```bash
sudo reboot
```

Pi відпадає ~1 хв, потім знову доступний. Сервіси піднімаються самі.

## Shutdown (перед відключенням від живлення)

```bash
sudo shutdown -h now
```

Не висмикуй кабель без цього — можна пошкодити SD-карту.

## БД

```bash
# розмір:
ls -lh ~/agro/server/agro.db

# кількість вимірів:
sqlite3 ~/agro/server/agro.db "SELECT COUNT(*) FROM measurement;"

# останні алерти:
sqlite3 ~/agro/server/agro.db "SELECT * FROM storedalert ORDER BY created_at DESC LIMIT 5;"

# видалити старі виміри (все крім останніх 7 днів):
sqlite3 ~/agro/server/agro.db "DELETE FROM measurement WHERE timestamp < datetime('now', '-7 days'); VACUUM;"
```

## Здоровʼя Pi

```bash
free -h                    # оперативна памʼять
df -h /                    # диск
vcgencmd measure_temp      # температура CPU
top -bn1 | head -20        # процеси
```

## Health-endpoint (з будь-якої машини в мережі)

```bash
curl http://agro-pi.local:8008/health
# {"status":"ok"}
```
