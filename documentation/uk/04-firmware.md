# Прошивки

`firmware/` — PlatformIO, Arduino-фреймворк, C++. Плата в усіх проєктах —
`heltec_wifi_lora_32_V3`. Бібліотеки: RadioLib 6.6 (SX1262), ThingPulse
SSD1306 (OLED), Adafruit SHT31.

## Проєкти

| Папка | Роль | Стан |
|---|---|---|
| `gateway/` | LoRa → USB NDJSON, RSSI/SNR, OLED зі смужками сигналу | ✅ у бою на Pi |
| `greenhouse-node-lowpower/` | **бойовий вузол**: цикл «прокинувся → виміряв → передав → deep sleep» | 🔧 працює; датчик ще на 3V3 |
| `greenhouse-node/` | стендовий вузол: 1 Гц, OLED, Serial, три датчики ґрунту з медіаною | ✅ опорний для етапу 3 |
| `soil-calibration/` | стенд калібрування: 4 канали, CSV по USB, команди доливу, Wi-Fi у хмару | 🔧 активний експеримент |
| `node_sensor/` | — | ⚠️ порожня папка, буде прибрана |

Чому три прошивки, а не одна з прапорцями: бойовій потрібна тиша (без Serial,
без OLED, сон), стенду — навпаки безперервний Serial, команди й живий екран.
Спільний код між ними невеликий; ціна дублювання менша за ціну
`#ifdef`-лабіринту.

## Бойовий вузол: один цикл

```
старт (80 МГц) → RTC-журнал → причина ресету → Ve on
→ SHT31 (3 спроби × 150 мс) → LoRa begin 868.0 → прогрів ґрунту SOIL_WARMUP_MS
→ ґрунт → повітря → vbat → JSON → transmit → [OLED 5 с, якщо не таймерне пробудження]
→ radio.sleep → Ve off → deep sleep SLEEP_MINUTES
```

Уся логіка в `setup()`, `loop()` порожній — після deep sleep плата стартує з нуля.

Ключові константи (усі перекриваються `-D` при збірці):

| Константа | Дефолт | Що |
|---|---|---|
| `SLEEP_MINUTES` | 15 | період |
| `NODE_ID` | 0 | номер вузла → `nodes.yaml` на сервері |
| `SOIL_WARMUP_MS` | 10 000 | прогрів датчика; заміряно ~500 мс, **навмисно не скорочено** до етапу 3 |
| `SOIL_SENSOR_COUNT` | 1 | 1 або 3 (медіана) |
| `SOIL_RAW_MIN/MAX` | 300 / 3800 | межі «датчик поза діапазоном» → подія |
| `VBAT_LOW_V` / `VBAT_CRITICAL_V` | 3,50 / 3,35 | події батареї |
| `VBAT_DIVIDER` | 4,9 | дільник плати |

Перетворення ґрунту — `map(raw, 3000, 1200, 0, 100)`: тимчасове, буде замінене
кривою з калібрування.

### Середовища збірки (`platformio.ini`)

| env | прапорці | навіщо |
|---|---|---|
| `lowpower_15min` (типове) | `SLEEP_MINUTES=15` | бій |
| `lowpower_5min` | `SLEEP_MINUTES=5` | частіший цикл |
| `lowpower_probe` | + `SOIL_WARMUP_PROBE=60 AGRO_DEBUG_SERIAL` | дивитись криву прогріву по Serial |
| `lowpower_abtest` | + `SOIL_AB_TEST=60` | два вікна прогріву в одному циклі (`soil_fast`/`soil_slow`) |
| `sleep_win10` / `sleep_win30` | `SOIL_WARMUP_MS=10000` / `30000` | етап 3 |
| `sleep_win_ab` | `SOIL_WARMUP_MS=10000 SOIL_AB_TEST=30` | етап 3, парне порівняння |

## Шлюз

Неблокуючий прийом (`setPacketReceivedAction` + `startReceive`), до `MAX_NODES 3`
вузлів (`node_id` 0/1/2), дописує `"rssi","snr"` у JSON і друкує рядок у
Serial 115200. Раз на 10 битих CRC — подія `crc_error_burst`. На OLED — смужки
сигналу по вузлах; вузол вважається «зниклим» через
`NODE_INTERVAL_S × 2,5 + 10 с` — для вузла зі сном 15 хв збирати з
`-DNODE_INTERVAL_S=900`, інакше екран покаже його мертвим 99 % часу (впливає
лише на екран).

## Події заліза

Вузол несе 64-байтний слот у RTC-пам'яті (`.rtc_noinit`): `magic`, `bootCount`,
`errFlags`, `errSeq`, кільце 24 записів (код 4 біти + boot 12 біт). У пакет іде
`err` (бітмаска) + `eseq`; гаситься лише після успішного `transmit`, тому подія
не губиться, якщо передача не вдалась.

| Код вузла | Назва | | Код шлюза | Назва |
|---|---|---|---|---|
| 1 | `cold_boot` | | 1 | `lora_init_failed` |
| 2 | `reset_brownout` | | 2 | `rx_failed` |
| 3 | `reset_panic` | | 3 | `crc_error_burst` |
| 4 | `reset_watchdog` | | 4 | `boot` |
| 5 | `lora_init_failed` | | | |
| 6 | `lora_tx_failed` | | | |
| 7 | `i2c_sensor_missing` | | | |
| 8 | `air_sensor_nan` | | | |
| 9 | `soil_sensor_out_of_range` | | | |
| 10 | `battery_low` | | | |
| 11 | `battery_critical` | | | |

Приклад: `err: 68` = біти 2 і 6 = brownout + невдала передача. Таблиця назв
дзеркалиться в `server/events.py` — міняти синхронно. Специфікація —
`docs/довідка/події.md`.

## Збірка й заливка

Компіляція — у Docker (образ `agro-firmware`, Python 3.11-slim, бо на хості
під 3.14 не збирається `cryptography` для esptool). Заливка — з хоста через
локальний `pio` (pipx, Python 3.11): Docker Desktop на macOS не прокидає USB.

```bash
make setup-host                                   # раз: pipx + platformio під 3.11
make build  PROJECT=greenhouse-node-lowpower      # Docker
make upload PROJECT=greenhouse-node-lowpower      # хост, порт із SERIAL_PORT
make deploy PROJECT=... PIO_ENV=sleep_win30       # build + upload у конкретний env
make deploy-lowpower SLEEP=5                      # = PIO_ENV=lowpower_5min
make deploy-soil-cal                              # стенд
make monitor PROJECT=...                          # Serial 115200
```

`SERIAL_PORT` — змінна оточення (`/dev/cu.usbserial-0001` за замовчуванням),
не зашита в код. Для шлюза заливка йде **на Pi** (він висить на USB там):

```bash
make deploy-gateway      # rsync → ssh: stop agro-adapter → pio upload → start agro-adapter (trap)
```

Телеметрія переривається до хвилини. Якщо авто-reset CP210x не спрацював —
тримати BOOT.

Секрети для Wi-Fi-збірок (`wifi_field*`) — у `firmware/.env` (гітігнорений):
`WIFI_SSID`, `WIFI_PASS`, `INGEST_URL`, `INGEST_TOKEN`; Makefile підставляє їх
як `-D` лише коли `PIO_ENV` починається з `wifi_`, і відмовляється заливати без них.

## Домовленості

- **Serial геть із бойової прошивки** — вмикається лише `-DAGRO_DEBUG_SERIAL`.
- **Відсутнє поле ≠ нуль**: якщо датчик не відповів, поля в JSON немає. Стендовий
  `greenhouse-node` ще ставить нулі — не копіювати.
- **Події замість логів**: усе, що варто знати про стан плати, їде бітмаскою в пакеті.
- **Число без умов нічого не значить**: прогрів завжди з паузою без живлення
  (`T±10 = 0,1 с @ off 120 с`), показник ґрунту — з версією датчика й напругою.

## Відомі борги

- Датчик ґрунту у вузлі живиться з 3V3, не з `Ve` → 15 мА цілодобово.
- `SOIL_WARMUP_MS` переплачує ~20× — чекає етапу 3.
- `map()` → крива з калібрування.
- Параметри LoRa (SF/BW/потужність) не задані явно.
- `node_sensor/` — прибрати з docker-compose і README.
