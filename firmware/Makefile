PROJECT ?= greenhouse-node
BOARD ?= heltec_wifi_lora_32_V3
HOST_PYTHON ?= $(shell command -v python3.11 2>/dev/null || command -v python3)
# USB-порт плати — специфічний для конкретної машини/підключення (macOS номерує
# /dev/cu.usbserial-N по-різному щоразу), тому не хардкодимо в platformio.ini,
# а підставляємо через env: перевизнач при потребі, напр.
# `make upload PROJECT=gateway SERIAL_PORT=/dev/cu.usbserial-5`.
SERIAL_PORT ?= /dev/cu.usbserial-0001

.PHONY: setup-host _ensure-host-pio init build upload deploy monitor shell clean

# Локальний pio (для upload/monitor — потребують USB, Docker на macOS туди не дотягується).
# Ставимо саме під Python 3.11: на 3.13+/3.14 pip не завжди має готові wheel-и
# для esptoolpy-залежностей (cryptography) і падає на збірці з джерела.
setup-host:
	pipx install --force --python $(HOST_PYTHON) platformio
	@# pipx інколи не кладе pip у сам venv — PlatformIO викликає `python -m pip`
	@# напряму зсередини venv (для esptoolpy-залежностей), тож підкладаємо його тут.
	~/.local/pipx/venvs/platformio/bin/python -m ensurepip --upgrade

# upload/monitor самі підтягують pio, якщо його ще нема на хості.
_ensure-host-pio:
	@command -v pio >/dev/null 2>&1 || $(MAKE) setup-host

init:
	docker compose run --rm $(PROJECT) project init --board $(BOARD)

build:
	docker compose run --rm $(PROJECT) run

upload: _ensure-host-pio
	SERIAL_PORT=$(SERIAL_PORT) pio run -d $(PROJECT) -t upload

# build (в Docker) + upload (з хоста) одним викликом.
deploy: build upload

monitor: _ensure-host-pio
	SERIAL_PORT=$(SERIAL_PORT) pio device monitor -d $(PROJECT)

shell:
	docker compose run --rm --entrypoint sh $(PROJECT)

clean:
	docker compose run --rm $(PROJECT) run -t clean
