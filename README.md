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

**Full hierarchy — from the shared bot down to individual sensors on each isolated instance:**

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
             └──────┬──────┘            └──────┬──────┘            └──────┬──────┘
                    │                          │                          │
                    ▼                          ▼                          ▼
             ┌─────────────┐            ┌─────────────┐            ┌─────────────┐
             │ greenhouse  │            │ greenhouse  │            │ greenhouse  │
             │   (LoRa)    │            │   (LoRa)    │            │   (LoRa)    │
             └──────┬──────┘            └──────┬──────┘            └──────┬──────┘
                    │                          │                          │
              ┌─────┼─────┐              ┌─────┼─────┐              ┌─────┼─────┐
              ▼     ▼     ▼              ▼     ▼     ▼              ▼     ▼     ▼
            ┌───┐ ┌───┐ ┌───┐          ┌───┐ ┌───┐ ┌───┐          ┌───┐ ┌───┐ ┌───┐
            │s-1│ │s-2│ │s-3│          │s-1│ │s-2│ │s-3│          │s-1│ │s-2│ │s-3│
            └───┘ └───┘ └───┘          └───┘ └───┘ └───┘          └───┘ └───┘ └───┘
```

Each `agro-#N` is a fully isolated `agro-server` deployment with its own database and greenhouses. The `agro-bot-hub` routes notifications between the shared Telegram bot and the correct instance, based on a per-user token generated at Telegram-linking time.

Model selection is a `.env` switch: `NOTIFIER=telegram_direct` (own bot) vs `NOTIFIER=telegram_hub` (shared hub). See [`docs/alerts.md`](./docs/alerts.md) for the detailed design.

## Tech stack

- **Hardware:** Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262, 868 MHz), Raspberry Pi 5, SHT31, capacitive soil moisture v1.2.
- **Backend:** Python 3.13, FastAPI, SQLModel, SQLite.
- **Firmware:** C++ (Arduino / PlatformIO).
- **Notifications:** Telegram Bot API.
