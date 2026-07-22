# Прошивки (firmware)

PlatformIO-проєкти для LoRa-вузлів системи поливу теплиць.
Архітектура й протокол обміну описані в [`docs/greenhouse_architecture.md`](../docs/greenhouse_architecture.md).

## Структура

- `greenhouse-node/` — вузол теплиці (Heltec WiFi LoRa 32 V3, 863-928 МГц): сенсори, клапан, LoRa-звʼязок з базою.
- `gateway/` — база (LoRa-приймач/передавач, збір телеметрії).
- `node_sensor/` — заготовка під додаткові сенсорні вузли.

## Встановлення

PlatformIO потрібен локально лише для `upload`/`monitor` (Docker на macOS не має доступу до USB).
Ставити саме під Python 3.11 через pipx — на 3.13+/3.14 pip часто не має готових wheel-ів
для залежностей `esptoolpy` (наприклад `cryptography`) і падає на збірці з джерела:

```
make setup-host
```

(разова дія — після встановлення venv лишається на Python 3.11, повторювати не треба).
`make upload`/`make monitor` самі перевіряють наявність `pio` і ставлять його, якщо відсутній —
викликати `setup-host` вручну треба лише якщо venv зламався (наприклад, стояв не під той Python).

Перевірка:

```
pio --version
```

## Збірка через Docker

Компіляція прошивки може йти в контейнері (без встановлення PlatformIO на хост).
**Заливка на плату (`upload`) все одно виконується з хоста** — Docker Desktop на macOS
не прокидає USB-порти в контейнер.

Через `docker-compose.yml` (по одному сервісу на кожен проєкт: `greenhouse-node`, `gateway`, `node_sensor`,
усі використовують один спільний образ `agro-firmware` — збирається один раз, для решти береться з кеша):

```
cd firmware
docker compose run --rm greenhouse-node project init --board heltec_wifi_lora_32_V3
docker compose run --rm greenhouse-node run
```

Папку проєкту (`greenhouse-node/`) створювати вручну не треба — Docker сам створює її для bind-mount, якщо вона відсутня.

Або напряму, без compose:

```
docker build -t agro-firmware .
docker run --rm -v "$(pwd)/greenhouse-node:/project" agro-firmware run
```

## Makefile-скорочення

Щоб не набирати довгі `docker compose run` щоразу, є `Makefile` з готовими цілями
(за замовчуванням працює з `greenhouse-node` / `heltec_wifi_lora_32_V3`, змінюється через `PROJECT=` і `BOARD=`):

```
make init      # pio project init --board ... (один раз на проєкт)
make build     # збірка через контейнер (docker compose run)
make upload    # заливка на плату — з хоста, бо USB
make monitor   # серійна консоль — з хоста
make shell     # зайти всередину контейнера
make clean     # очистити збірку
```

Приклад для іншого проєкту: `make init PROJECT=gateway BOARD=<board-id>`.

## Заливка прошивки на плату

З хоста, локально встановленим `pio`:

```
cd firmware/greenhouse-node
pio run -t upload
```

## Плата

Heltec WiFi LoRa 32 V3, HF-варіант (863-928 МГц, підходить під EU868).
PlatformIO board ID: `heltec_wifi_lora_32_V3`.

## OLED дисплей

- Розмір: 0.96"
- Роздільність: 128×64
- X: 0–127, Y: 0–63
- Шрифт: `ArialMT_Plain_10`, ~10-12px/рядок, тільки латиниця (кирилицю не малює)
- Останній рядок без обрізання знизу: `y=52`
