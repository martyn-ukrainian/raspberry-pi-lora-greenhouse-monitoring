> [Українська версія](./README-UA.md)

# Agro — LoRa Greenhouse Monitoring on Raspberry Pi

Field-deployed monitoring system for a small commercial greenhouse operation. Battery-friendly ESP32 sensor nodes report air temperature, humidity, and soil moisture over 868 MHz LoRa radio to a Raspberry Pi gateway. The Pi runs a Python backend, stores every reading in SQLite, and pushes plain-language alerts through a Telegram bot.

The primary user is a non-technical daily operator. Design priority: readable Telegram notifications over complex dashboards.

This repository doubles as a portfolio artefact for embedded / defence-sector roles: C++ firmware on ESP32, LoRa radio at 868 MHz, full-stack Python backend, and a real field deployment on a working farm.

## Scale

Eight greenhouses total: five active cucumber and tomato houses, one seedling house that acts as the system hub, one new standard house, and one new double-film house. Distances between houses: 50–250 m. Phase 1 covers three sensor nodes and one gateway.

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

## Soil sensor characterisation (v1.2)

The soil-moisture reading is the one measurement in this system that was never
grounded in anything: `soilRawToPercent()` shipped with hand-waved constants
(`map(raw, 3000, 1200, 0, 100)`) that nobody had verified. A dedicated bench
firmware — [`firmware/soil-calibration`](./firmware/soil-calibration) — now
measures the sensors instead of guessing at them. It emits raw ADC and
millivolts over USB, because a percentage is the *output* of calibration and
useless as its input.

Measured on a **v1.2 TENSTAR capacitive sensor at 3.3 V** (2026-08-22):

| State | raw | mV | noise σ |
|---|---:|---:|---:|
| air | 2646 | 2229 | ~4 mV |
| dry soil | 2489 | 2101 | 0.7–2.8 mV |

**Warm-up.** How long the sensor needs after power is applied before its output
can be trusted — the question that decides battery life on a sleeping node,
since every wake-up is a cold start. Metric `T±k` (defined in
[`docs/калібрування-ґрунту.md`](./docs/калібрування-ґрунту.md)) is the time
after which the curve no longer leaves ±k mV of its final value:

| Time without power | `T±50` | `T±20` | `T±10` |
|---|---:|---:|---:|
| 3 s | 0.3 s | 0.3 s | **0.4 s** |
| 120 s | 0.1 s | 0.4 s | **0.5 s** |

A 40× longer power-off changed nothing: the peak-detector capacitor bottoms out
within seconds and recharges in half a second. The low-power firmware currently
budgets **10 000 ms** for this — roughly a 20× overprovision, and by the figures
in [`docs/power-budget.md`](./docs/power-budget.md) that is most of a doubling
in battery life.

### v1.2 vs v2.0

Two specimens of each version, measured on the same ADC channel, in the same
hole, in the same soil — so the difference can only come from the sensor.

| | v1.2 | v2.0 | |
|---|---:|---:|---|
| air | 2229 mV | 2721 mV | |
| dry soil | 2101 mV | 2414 mV | |
| **span air→soil** | **128 mV** | **307 mV** | 2.4× wider |
| noise σ | 2.9 mV | 1.05 mV | 2.8× quieter |
| **span / σ** | **44** | **292** | **6.6× finer resolution** |
| `T±10` warm-up | 0.4 s | 0.1 s | 4× faster |

v2.0 resolves soil moisture roughly **6.6× finer** and warms up **4× faster**.
Since the battery node has no 5 V rail at all — the Heltec's 5V pin is fed from
USB — and v1.2 is a 5 V design whose range is compressed at 3.3 V, the reserve
batch is the one that belongs in the node.

**A control measurement decided how much of this to believe.** `v1.2-A` was
re-inserted at the end of the session and read 9 mV away from its own first
measurement — that is the reproducibility of the whole procedure, not ADC noise.
Against that yardstick the 310 mV version gap is 34× the error and stands; the
2–5 mV spread *between specimens of one batch* is below it and was withdrawn as
unmeasured.

Numbers, method, and the `T±k` definition:
[`docs/калібрування-ґрунту.md`](./docs/калібрування-ґрунту.md).

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
