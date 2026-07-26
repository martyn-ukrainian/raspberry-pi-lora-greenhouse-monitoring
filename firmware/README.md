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
make deploy    # build + upload одним викликом
make monitor   # серійна консоль — з хоста
make shell     # зайти всередину контейнера
make clean     # очистити збірку
```

Приклад для іншого проєкту: `make init PROJECT=gateway BOARD=<board-id>`.

**Порт плати** (`SERIAL_PORT`) не хардкодиться в `platformio.ini` (`upload_port`/`monitor_port` там читають
`${sysenv.SERIAL_PORT}`) — macOS перенумеровує `/dev/cu.usbserial-N` по-різному щоразу, тож значення
задається через змінну середовища, з дефолтом у `Makefile`:

```
make upload PROJECT=gateway SERIAL_PORT=/dev/cu.usbserial-5
```

Якщо кілька плат підключені одночасно — перевір справжній порт кожної через `ls /dev/cu.*`.

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

## Пінаут (Heltec WiFi LoRa 32 V3)

Зведено з `schema.png` (фото пінаута плати) + пінаут з `main.cpp`. Плата має два роз'єми — J3 (лівий) і J2 (правий).

**Header J3 (лівий):**
| Pin | GPIO | Додатково |
|---|---|---|
| 18 | GPIO7 | ADC1_CH6 |
| 17 | GPIO6 | ADC1_CH5 |
| 16 | GPIO5 | ADC1_CH4 |
| 15 | GPIO4 | ADC1_CH3 |
| 14 | GPIO3 | ADC1_CH2 |
| 13 | GPIO2 | ADC1_CH1 |
| 12 | GPIO1 | ADC1_CH0, VBAT_Read |
| 11 | GPIO38 | SUBSPIWP, FSPIWP |
| 10 | GPIO39 | MTCK |
| 9 | GPIO40 | MTDO — використано як **SCL** (2-га I2C, повітряний сенсор) |
| 8 | GPIO41 | використано як **SDA** (2-га I2C, повітряний сенсор) |
| 7 | GPIO42 | |
| 6 | GPIO45 | |
| 5 | GPIO46 | |
| 4 | GPIO37 | ADC_Ctrl↑, SUBSPIQ, FSPIQ, SPIDQS |
| 3 | 3V3 | |
| 2 | 3V3 | VIN (для 2-ї I2C) |
| 1 | GND | GND (для 2-ї I2C) |

**Header J2 (правий):**
| Pin | GPIO | Додатково |
|---|---|---|
| 18 | GPIO19 | U1RST, ADC2_CH9 |
| 17 | GPIO20 | U1CTS, ADC2_CH9 |
| 16 | GPIO21 | **OLED_RST** |
| 15 | GPIO26 | SPICS1 |
| 14 | GPIO48 | |
| 13 | GPIO47 | |
| 12 | GPIO33 | SPIIO4, FSPIHD, SUBSPIHD |
| 11 | GPIO34 | SPIIO5, FSPICS0, SUBSPICS0 |
| 10 | GPIO35 | SPIIO6, FSPID, SUBSPID, LED_Write↑ |
| 9 | GPIO36 | SPIIO7, FSPICLK, SUBSPICLK, **Vext_Ctrl**↑ |
| 8 | GPIO0 | USER_SW↑ (кнопка PRG/BOOT) |
| 7 | — | RST_SW↑ (кнопка RESET, не GPIO) |
| 6 | GPIO43 | U0TXD, CP2102_RX |
| 5 | GPIO44 | U0RXD, CP2102_TX |
| 4 | Ve | |
| 3 | Ve | |
| 2 | 5V | |
| 1 | GND | |

**LoRa (SX1262, SPI)** — не на цих роз'ємах, окремі внутрішні піни:
| Пін | GPIO |
|---|---|
| NSS (CS) | 8 |
| SCK | 9 |
| MOSI | 10 |
| MISO | 11 |
| RST | 12 |
| BUSY | 13 |
| DIO1 | 14 |

**OLED (I2C, `Wire`) + живлення периферії:**
| Пін | GPIO |
|---|---|
| SDA | 17 |
| SCL | 18 |
| OLED_RST | 21 (= J2 pin 16) |
| Vext_Ctrl | 36 (= J2 pin 9, LOW = увімкнено) |

**Друга I2C-шина (повітряний сенсор SHT31) — окрема від OLED, потребує `Wire1`. Схема підключення, Header J3:**



```
Pin #:    1     2     3     4      5      6      7      8      9      10     11     12
GPIO2:   GND   3V3   3V3  GPIO37 GPIO46 GPIO45 GPIO42 GPIO41 GPIO40 GPIO39 GPIO38 GPIO1
          |     |                                |      |
       ---+-----+--------------------------------+------+---------------------------------------
         GND   VIN                              SCL    SDA
       (SHT31)(SHT31)                         (SHT31) (SHT31)
```

```
Pin #:    1     2     3     4      5      6      7      8      9      10     11     12
GPIO2:   GND   3V3   3V3  GPIO37 GPIO46 GPIO45 GPIO42 GPIO41 GPIO40 GPIO39 GPIO38 GPIO1
          |     |                                |      |
       ---+-----+--------------------------------+------+---------------------------------------
         GND   VIN                              SCL    SDA
       (SHT31)(SHT31)                         (SHT31) (SHT31)
```

Тобто фізично на плату SHT31: **GND → pin 1, VIN → pin 2, SCL → pin 7 (GPIO42), SDA → pin 8 (GPIO41)**.
Піни 3-6 і 9-12 в цій схемі не використовуються.

**Вільні ADC1-піни під сенсори** (ADC1, не ADC2 — ADC2 конфліктує з Wi-Fi/LoRa), Header J3:
| Pin | GPIO | ADC channel |
|---|---|---|
| 12 | GPIO1 | ADC1_CH0 |
| 13 | GPIO2 | ADC1_CH1 |
| 14 | GPIO3 | ADC1_CH2 |
| 15 | GPIO4 | ADC1_CH3 |
| 16 | GPIO5 | ADC1_CH4 |
| 17 | GPIO6 | ADC1_CH5 |
| 18 | GPIO7 | ADC1_CH6 |

Датчики вологості ґрунту (ємнісні, v1.2) зараз підключені на **GPIO2/3/4** (Header J3, піни 13/14/15).

VIN = фіолетовий
GIN = сірий

SCL = білий - 7 (GPIO42)
SDA = чорний - 8 (GPIO41)
