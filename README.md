> [Українська версія](./README-UA.md)

# Agro — LoRa Greenhouse Monitoring on Raspberry Pi

Field-deployed monitoring system for a small family greenhouse operation. Battery-friendly ESP32 sensor nodes report air temperature, humidity, and soil moisture over 868 MHz LoRa radio to a Raspberry Pi gateway. The Pi runs a Python backend, stores every reading in SQLite, and pushes plain-language alerts through a Telegram bot.

The primary user is the project owner's father — a non-technical daily operator. Design priority: readable Telegram notifications over complex dashboards.

This repository doubles as a portfolio artefact for embedded / defence-sector roles: C++ firmware on ESP32, LoRa radio at 868 MHz, full-stack Python backend, and a real field deployment on a working farm.

## Scale

Eight greenhouses total: five active cucumber and tomato houses, one seedling house that acts as the system hub, one new standard house, and one new double-film house. Distances between houses: 50–250 m. Phase 1 covers three sensor nodes and one gateway.

## Architecture

Each greenhouse is an autonomous node built on a **Heltec WiFi LoRa 32 V3** (ESP32-S3 with SX1262). Each node carries an **SHT31** for air temperature and humidity, plus a **v1.2 capacitive soil-moisture sensor**. Readings are transmitted over **868 MHz LoRa**. A gateway node in the seedling greenhouse aggregates all traffic and forwards it via USB to a **Raspberry Pi 5**. The Pi runs a **Python + FastAPI** backend backed by **SQLite (via SQLModel)**, applies threshold logic, and sends notifications through a **Telegram bot**.

Full breakdown in [`docs/architecture.md`](./docs/architecture.md).

## Repository layout

- **`server/`** — Python backend that ingests, stores, and serves measurements. Developed on a laptop first, then copied to the Pi unchanged. Stack: Python 3.13, FastAPI, SQLModel, SQLite.
- **`firmware/`** — Node and gateway firmware in C++ (Arduino / PlatformIO).
- **`docs/`** — Architecture notes, repository structure, and an ongoing log of empirical thresholds and decisions.

## Phases

1. **Monitoring.** Sensor ingestion, storage, graphs, Telegram alerts. No actuators.
2. **Irrigation.** Pump or valve on a schedule, plus soil-moisture-driven control.
3. **Ventilation and temperature.** Window actuator or extraction fan.

## Tech stack

- **Hardware:** Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262, 868 MHz), Raspberry Pi 5, SHT31, capacitive soil moisture v1.2.
- **Backend:** Python 3.13, FastAPI, SQLModel, SQLite.
- **Firmware:** C++ (Arduino / PlatformIO).
- **Notifications:** Telegram Bot API.
