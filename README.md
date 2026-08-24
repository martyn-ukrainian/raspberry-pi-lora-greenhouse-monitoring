> [Українська версія](./README-UA.md)

# Agro — LoRa Greenhouse Monitoring on Raspberry Pi

Field-deployed monitoring system for a small commercial greenhouse operation. Battery-friendly ESP32 sensor nodes report air temperature, humidity, and soil moisture over 868 MHz LoRa radio to a Raspberry Pi gateway. The Pi runs a Python backend, stores every reading in SQLite, and pushes plain-language alerts through a Telegram bot.

The primary user is a non-technical daily operator. Design priority: readable Telegram notifications over complex dashboards.

This repository doubles as a portfolio artefact for embedded / defence-sector roles: C++ firmware on ESP32, LoRa radio at 868 MHz, full-stack Python backend, and a real field deployment on a working farm.

## Scale

Eight greenhouses total: five active cucumber and tomato houses, one seedling house that acts as the system hub, one new standard house, and one new double-film house. Distances between houses: 50–250 m. Phase 1 covers three sensor nodes and one gateway.

## LoRa Node

Each greenhouse runs one autonomous LoRa node built on a **Heltec WiFi LoRa 32 V3** (ESP32-S3 + SX1262). It carries an SHT31 (air temperature/humidity) and a capacitive soil-moisture sensor, and reports to the gateway over 868 MHz LoRa.

![Heltec WiFi LoRa 32 V3 pinout](firmware/schema.jpg)

Full pinout, wiring, and firmware — [`firmware/README.md`](./firmware/README.md).

## Soil sensor characterisation (v1.2 vs v2.0)

Measured in a 1 kg soil bucket, water added in increments, output logged as raw
ADC + mV over USB by a dedicated bench firmware
([`firmware/soil-calibration`](./firmware/soil-calibration)). Two specimens of
each version, same ADC channel, same hole, same soil — so the difference can
only come from the sensor.

| | v1.2 | v2.0 | |
|---|---:|---:|---|
| air | 2229 mV | 2721 mV | |
| dry soil | 2101 mV | 2409 mV | |
| noise σ | 2.9 mV | **1.05 mV** | 2.8× quieter |
| `T±10` warm-up | 0.4 s | **0.1 s** | 4× faster |
| sensitivity to water | 12.6 mV/mL | 12.1 mV/mL | **the same** |

**v2.0 wins, but not for the reason the air-to-soil span suggested.** That span
is 2.4× wider on v2.0 — and it turned out to be a poor predictor: both versions
move almost identically per millilitre of water. The real advantage is **≈3×,
and it is entirely noise**. Since a battery node has no 5 V rail at all (the
Heltec's 5V pin is fed from USB) and v1.2 is a 5 V design running compressed at
3.3 V, the reserve batch is the one that belongs in the node.

A control measurement decided how much of this to believe: `v1.2-A` was
re-inserted at the end of the session and read **9 mV** away from its own first
measurement — that is the reproducibility of the whole procedure, not ADC noise.
The 310 mV version gap is 34× that error and stands; the 2–5 mV spread between
specimens of one batch is below it and was withdrawn as unmeasured.

### Calibration curve: water added vs sensor output

![Calibration curve](./firmware/soil-calibration/data/calibration_curve.png)

Each successive addition moves the reading less than the last — the sensor
saturates. Modelled as geometric decay of the per-step delta, which is the same
exponential written two ways:

```
mV(V) = C + A · r^(V / step)     ≡     C + A · e^(−V/k),   k = −step / ln r
```

Measured on v2.0: **r ≈ 0.35** — each equal 50 mL step does about 35% of the
previous one's work; equivalently **k ≈ 52 mL**. Fit RMS 9.7 mV over a 1535 mV
range, i.e. 0.6%.

The consequence is a **240× spread in sensitivity**: 12.0 mV per mL in dry soil,
0.05 mV per mL in a watered greenhouse bed.

### Why the reported percentage is logarithmic

![Moisture scale](./firmware/soil-calibration/data/scale_vs_water.png)

Plotted against *water*, the logarithmic scale is a straight line and `map()`
is the bent one — which of the two matches physics is visible at a glance. Half
the water added reads **50%**; the linear scale calls the same soil **94%**.

The bars are the same fact per equal 25 mL portion: linear gives the first
portion **38 points** and the last **0.3** — a 128× difference for the same
amount of water. Logarithmic gives every portion the same weight.

A linear `map()` spreads percentage evenly across *voltage*, not across *water*,
so it eats the whole wet half of the range — exactly where a greenhouse lives.
Its midpoint reads 89% where the soil is only halfway.

The firmware converts through the measured curve instead, and ships the **raw
ADC alongside the percentage**: the percentage is an interpretation whose scale
is not final yet, while the raw value is the measurement and lets the entire
history be recomputed without reflashing nodes in the field.

This was arrived at from our own measurements, and only checked against the
literature afterwards — where it turns out to be the recommended practice, not
an invention here. Topp's 1980 equation is a cubic polynomial; field-calibration
work describes both *"a two-point model and exponential curve fitting"*, noting
that **two-point linear is simpler and more practical for the field, while curve
fitting gives higher accuracy when properly calibrated for the specific soil**.
The same work is explicit that **the manufacturer's calibration is not enough and
soil-specific recalibration is needed** — which is what the two days of bucket
measurements were.

One honest difference: the literature's reference is gravimetric water content
(weigh and oven-dry); ours is water added. Our scale is therefore **relative** and
does not claim θ in m³/m³.

Method, numbers and sources: [`docs/нелінійна-шкала.md`](./docs/нелінійна-шкала.md),
[`docs/калібрування-ґрунту.md`](./docs/калібрування-ґрунту.md).

## Architecture

Each greenhouse is an autonomous node built on a **Heltec WiFi LoRa 32 V3** (ESP32-S3 with SX1262). Each node carries an **SHT31** for air temperature and humidity, plus a **v1.2 capacitive soil-moisture sensor**. Readings are transmitted over **868 MHz LoRa**. A gateway node in the seedling greenhouse aggregates all traffic and forwards it via USB to a **Raspberry Pi 5**. The Pi runs a **Python + FastAPI** backend backed by **SQLite (via SQLModel)**, applies threshold logic, and sends notifications through a **Telegram bot**.

Full breakdown in [`docs/architecture.md`](./docs/architecture.md).

### Data flow

```
                    Greenhouse
  ┌───────────────────────────────────────────────────┐
  │                                                   │
  │   ┌───────────────────────────────────────────┐   │
  │   │  LoRa Node — Heltec V3 (ESP32-S3+SX1262)  │   │
  │   │                                           │   │
  │   │   ┌──────────────┐  ┌──────────────────┐  │   │
  │   │   │ SHT31        │  │ Capacitive Soil  │  │   │
  │   │   │ air temp     │  │ Moisture v1.2    │  │   │
  │   │   │ air humidity │  │                  │  │   │
  │   │   └──────────────┘  └──────────────────┘  │   │
  │   └───────────────────┬───────────────────────┘   │
  └───────────────────────┼───────────────────────────┘
                          │ LoRa 868 MHz
                          ▼
                  ┌───────────────────┐
                  │  Gateway Node     │
                  │  (Heltec V3)      │
                  └─────────┬─────────┘
                            │ USB
                            ▼
                  ┌───────────────────┐
                  │  Raspberry Pi 5   │
                  │  FastAPI + SQLite │
                  │  Alerts logic     │
                  └─────────┬─────────┘
                            │ Telegram Bot API
                            ▼
                  ┌───────────────────┐
                  │  Operator         │
                  │  (Telegram app)   │
                  └───────────────────┘
```

### Logical hierarchy

Each greenhouse hosts one LoRa node, and each LoRa node carries multiple sensors. The backend organises configuration and alerting around this three-level structure:

```
agro-server
├── greenhouses/
│   ├── greenhouse-1  "Seedling (cucumbers)"
│   │   └── LoRa Node (Heltec V3)
│   │       ├── SHT31       → air_temperature, air_humidity
│   │       └── Capacitive  → soil_moisture
│   ├── greenhouse-2  "Cucumbers primary"
│   │   └── LoRa Node
│   │       ├── SHT31       → air_temperature, air_humidity
│   │       └── Capacitive  → soil_moisture
│   └── ...
└── notifiers/
    └── Telegram  (direct bot OR via bot hub)
```

Thresholds and alert config live per-greenhouse. Each sensor may override dwell time; a shared `defaults` block applies otherwise.

## Repository layout

- **`server/`** — Python backend that ingests, stores, and serves measurements. Developed on a laptop first, then copied to the Pi unchanged. Stack: Python 3.13, FastAPI, SQLModel, SQLite.
- **`firmware/`** — Node and gateway firmware in C++ (Arduino / PlatformIO).
- **`docs/`** — Architecture notes, repository structure, and an ongoing log of empirical thresholds and decisions.

## Phases

1. **Monitoring.** Sensor ingestion, storage, graphs, Telegram alerts. No actuators.
2. **Irrigation.** Pump or valve on a schedule, plus soil-moisture-driven control.
3. **Ventilation and temperature.** Window actuator or extraction fan.

## Deployment models

The codebase is designed to support **two distribution modes**, selected by configuration. Both modes run identical core code — only the notifier layer differs.

**Model A — Self-hosted (open source).** Each operator clones the repo, runs the server on their own Raspberry Pi, and creates their own Telegram bot via @BotFather. Full data ownership, no external dependencies.

```
    ┌──────────────────┐
    │ Operator's Bot   │  (created via @BotFather)
    │ (@Farmer1Bot)    │
    └────────┬─────────┘
             │
    ┌────────▼─────────┐
    │ agro-server #1   │  (own DB, own config)
    └──────────────────┘
```

**Model B — Central hub (SaaS).** A single Telegram bot serves many isolated `agro-server` instances. Users register on the hub, deep-link their account to the bot, and the hub routes messages to the correct instance. Enables managed deployments for non-technical operators.

```
                    ┌─────────────────────────┐
                    │  agro-bot-hub (SaaS)    │
                    │  @AgroMonitorBot        │
                    │  Route: token → server  │
                    └──────┬──────────────────┘
                           │
        ┌──────────────────┼──────────────────┐
        ▼                  ▼                  ▼
   ┌─────────┐        ┌─────────┐        ┌─────────┐
   │agro-#1  │        │agro-#2  │        │agro-#3  │
   │Farmer 1 │        │Farmer 2 │        │  SaaS   │
   └─────────┘        └─────────┘        └─────────┘
```

**Visual tree — top layer shows the isolation model, then only `agro-#1` and `greenhouse-1` are drilled down. `agro-#2` and `agro-#3` follow the same shape:**

```
                                      ┌──────────────────┐
                                      │  agro-bot-hub    │
                                      │ @AgroMonitorBot  │
                                      └────────┬─────────┘
                                               │
                    ┌──────────────────────────┼──────────────────────────┐
                    ▼                          ▼                          ▼
             ┌─────────────┐            ┌─────────────┐            ┌─────────────┐
             │  agro-#1    │            │  agro-#2    │            │  agro-#3    │
             │  Farmer 1   │            │  Farmer 2   │            │    SaaS     │
             └──────┬──────┘            └─────────────┘            └─────────────┘
                    │
           ┌────────┴────────┐
           ▼                 ▼
    ┌─────────────┐   ┌─────────────┐
    │greenhouse-1 │   │greenhouse-2 │
    │   (LoRa)    │   │   (LoRa)    │
    └──────┬──────┘   └─────────────┘
           │
     ┌─────┼─────┐
     ▼     ▼     ▼
   ┌───┐ ┌───┐ ┌───┐
   │s-1│ │s-2│ │s-3│
   └───┘ └───┘ └───┘
```

**Full expanded hierarchy — each isolated instance holds multiple greenhouses, each with its own sensors:**

```
agro-bot-hub  (@AgroMonitorBot)
│
├── agro-#1  (Farmer 1)
│   ├── greenhouse-1  (LoRa node)
│   │   ├── sensor-1  (SHT31 → air temperature)
│   │   ├── sensor-2  (SHT31 → air humidity)
│   │   └── sensor-3  (capacitive → soil moisture)
│   ├── greenhouse-2  (LoRa node)
│   │   ├── sensor-1
│   │   ├── sensor-2
│   │   └── sensor-3
│   └── ...  more greenhouses
│
├── agro-#2  (Farmer 2)
│   ├── greenhouse-1  (LoRa node)
│   │   ├── sensor-1
│   │   ├── sensor-2
│   │   └── sensor-3
│   ├── greenhouse-2
│   │   └── ...
│   └── ...
│
└── agro-#3  (SaaS)
    ├── greenhouse-1
    │   └── ...
    └── ...
```

Each `agro-#N` is a fully isolated `agro-server` deployment with its own database and greenhouses. The `agro-bot-hub` routes notifications between the shared Telegram bot and the correct instance, based on a per-user token generated at Telegram-linking time.

Model selection is a `.env` switch: `NOTIFIER=telegram_direct` (own bot) vs `NOTIFIER=telegram_hub` (shared hub). See [`docs/alerts.md`](./docs/alerts.md) for the detailed design.

## Tech stack

- **Hardware:** Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262, 868 MHz), Raspberry Pi 5, SHT31, capacitive soil moisture v1.2.
- **Backend:** Python 3.13, FastAPI, SQLModel, SQLite.
- **Firmware:** C++ (Arduino / PlatformIO).
- **Notifications:** Telegram Bot API.
