# Agro Server

FastAPI backend for the LoRa greenhouse monitoring system. Ingests sensor measurements over HTTP (from an on-device USB gateway or from a simulator), applies threshold-based alerting, and dispatches notifications to Telegram.

Runs on a Raspberry Pi 5 in production; also runs unchanged on a macOS/Linux dev laptop.

## Requirements

- Python 3.13
- [`uv`](https://docs.astral.sh/uv/) — dependency manager
- `socat` — only needed for testing the USB pipeline without real hardware (`brew install socat` on macOS)

## First-time setup

```bash
uv sync
cp .env.example .env
```

Edit `.env` and fill in:

- `TELEGRAM_TOKEN` — created via [@BotFather](https://t.me/BotFather)
- `TELEGRAM_CHAT_ID` — get from `https://api.telegram.org/bot<TOKEN>/getUpdates` after sending a message to your bot

## Running the server

```bash
uv run fastapi dev main.py
```

- API: <http://127.0.0.1:8000>
- Swagger UI: <http://127.0.0.1:8000/docs>
- Logs are streamed to stdout and written to `logs/agro.log` (rotated daily, 14 days kept).

## Simulators

Two simulators are provided for local development without real hardware. Use whichever matches the pipeline you want to test.

### HTTP simulator — `simulate.py`

Sends POST requests directly to `/measurements`. Bypasses the USB layer entirely. Useful when developing pure server logic (alerts, storage, notifications).

```bash
uv run python simulate.py
```

Emits one measurement every 5 seconds, cycling randomly through `greenhouse-1`, `greenhouse-2`, `greenhouse-3`. Adjust `NODES` and `INTERVAL_SECONDS` at the top of the file if you need a different mix or cadence.

### USB gateway simulator — `gateway_simulator.py` + `socat`

Emulates a real LoRa gateway sending NDJSON over a virtual serial port. Exercises the full USB pipeline including the `usb_adapter.py` translator. This is the pipeline that will run on the Raspberry Pi in production, so this is the more realistic test.

**Step 1.** Start the virtual serial pair (leave running in its own terminal):

```bash
socat -d -d \
    pty,raw,echo=0,link=/tmp/agro_gateway \
    pty,raw,echo=0,link=/tmp/agro_adapter
```

This creates two connected TTY endpoints:

- `/tmp/agro_gateway` — written by the simulator
- `/tmp/agro_adapter` — read by the USB adapter

**Step 2.** Start the USB adapter (in another terminal):

```bash
uv run python usb_adapter.py
```

**Step 3.** Start the gateway simulator (in yet another terminal):

```bash
uv run python gateway_simulator.py
```

The measurement flow becomes:

```
gateway_simulator → /tmp/agro_gateway → socat → /tmp/agro_adapter → usb_adapter → POST → server
```

When real hardware arrives, the only change is `SERIAL_PORT` in `usb_adapter.py` — swap `/tmp/agro_adapter` for the actual device path (e.g. `/dev/tty.usbmodem14201` on macOS or `/dev/ttyACM0` on Raspberry Pi OS).

## Forcing alerts during tests

Both simulators generate values inside the normal thresholds by default, so alerts rarely fire. To exercise the alerting pipeline end-to-end:

1. Temporarily set `dwell_minutes: 1` for `air_temperature` in `config/thresholds.yaml` (default is 3 minutes; 1 minute is faster to test).
2. Temporarily change the temperature range in the simulator you're running:
   ```python
   "air_temperature": round(random.uniform(38, 42), 1)  # forces "high" alerts
   ```
3. Restart the simulator and the server (thresholds are loaded once on startup).
4. Watch for a Telegram message after roughly one minute of continuous out-of-range values.

Search for `ТИМЧАСОВО` (Ukrainian for "temporary") comments in modified files to find and revert these test-only changes before committing.

## Tests

```bash
uv run pytest
```

## Linting and formatting

```bash
uv run ruff check .           # check for lint issues
uv run ruff check . --fix     # auto-fix what's safe
uv run ruff format .          # reformat the code
```

A pre-commit hook runs `ruff-check` automatically before every commit (configured at the repository root in `../.pre-commit-config.yaml`).

## Project layout

Only the parts that matter for day-to-day development:

- `main.py` — FastAPI app entry point, mounts the measurements router.
- `measurements.py` — `Measurement` SQLModel, repository, POST/GET endpoints, wires in `alerts.check()` and the notifier.
- `alerts.py` — threshold detection with dwell + cooldown, in-memory state, `SensorReading` protocol.
- `thresholds.py` + `config/thresholds.yaml` — per-greenhouse threshold config, loaded and validated with Pydantic at startup.
- `messages.py` + `config/messages.yaml` — localised strings (icons, sensor names, units) used by notifiers.
- `notifiers/` — `Notifier` protocol and concrete implementations (currently `telegram.py`).
- `usb_adapter.py` — reads NDJSON from a serial port, maps numeric `node_id` to human label via `config/nodes.yaml`, forwards to the server as HTTP POST.
- `simulate.py` — HTTP simulator.
- `gateway_simulator.py` — USB gateway simulator.
- `tests/` — pytest suite.
