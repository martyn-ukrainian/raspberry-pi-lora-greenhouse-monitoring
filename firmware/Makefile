PROJECT ?= greenhouse-node
BOARD ?= heltec_wifi_lora_32_V3
HOST_PYTHON ?= $(shell command -v python3.11 2>/dev/null || command -v python3)
# USB-порт плати — специфічний для конкретної машини/підключення (macOS номерує
# /dev/cu.usbserial-N по-різному щоразу), тому не хардкодимо в platformio.ini,
# а підставляємо через env: перевизнач при потребі, напр.
# `make upload PROJECT=gateway SERIAL_PORT=/dev/cu.usbserial-5`.
SERIAL_PORT ?= /dev/cu.usbserial-0001

# Проєкт з кількома середовищами збірки (зараз це лише greenhouse-node-lowpower,
# де середовище задає інтервал сну) треба збирати з явним `-e`, інакше pio візьме
# default_envs. Порожній PIO_ENV = стара поведінка, решту проєктів не зачіпає.
PIO_ENV ?=
PIO_ENV_ARG = $(if $(PIO_ENV),-e $(PIO_ENV),)

# Інтервал сну енергозберігаючої прошивки, хвилини. Має збігатись з іменем
# середовища в greenhouse-node-lowpower/platformio.ini (5 або 15).
SLEEP ?= 15

# gateway сидить на USB Pi, не на Mac — деплой іде через SSH, деталі в DEPLOY.md.
PI_HOST ?= agro@agro-pi.local
PI_DIR ?= agro
PI_GATEWAY_PORT ?= /dev/ttyUSB0

# Облікові дані Wi-Fi і приймача — з .env, який лежить поруч і не в git.
# Читаємо файлом, а не аргументом команди: інакше пароль осідає в історії
# оболонки, а звідти його вже не прибрати.
#
# Прапорці підставляються лише коли PIO_ENV починається з wifi_ — звичайні
# збірки лишаються байт у байт такими, як були.
-include .env
export WIFI_SSID WIFI_PASS INGEST_URL INGEST_TOKEN

WIFI_FLAGS = -DWIFI_SSID=\"$(WIFI_SSID)\" -DWIFI_PASS=\"$(WIFI_PASS)\" \
             -DINGEST_URL=\"$(INGEST_URL)\" -DINGEST_TOKEN=\"$(INGEST_TOKEN)\"
PIO_EXTRA_FLAGS = $(if $(filter wifi_%,$(PIO_ENV)),$(WIFI_FLAGS),)

# Прогін калібрувального стенду: ім'я йде в назву CSV-файлу, щоб прогони не
# зливались в одну купу. `make soil-log RUN=v12-vs-v20`.
RUN ?= run

.PHONY: setup-host _ensure-host-pio init build upload deploy deploy-lowpower deploy-soil-cal deploy-gateway soil-log soil-watch soil-summary monitor shell clean

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
	docker compose run --rm $(PROJECT) run $(PIO_ENV_ARG)

upload: _ensure-host-pio
	@$(if $(filter wifi_%,$(PIO_ENV)),\
	  test -n "$(WIFI_SSID)" || { echo "нема WIFI_SSID — заповни firmware/.env"; exit 1; }; \
	  test -n "$(WIFI_PASS)" || { echo "нема WIFI_PASS — заповни firmware/.env"; exit 1; }; \
	  echo "Wi-Fi: $(WIFI_SSID) -> $(INGEST_URL)";)
	SERIAL_PORT=$(SERIAL_PORT) PLATFORMIO_BUILD_FLAGS='$(PIO_EXTRA_FLAGS)' \
		pio run -d $(PROJECT) $(PIO_ENV_ARG) -t upload

# build (в Docker) + upload (з хоста) одним викликом.
deploy: build upload

# Енергозберігаюча прошивка вузла: інтервал сну обирається збіркою, не кодом.
#   make deploy-lowpower           -> 15 хв (дефолт)
#   make deploy-lowpower SLEEP=5   -> 5 хв
deploy-lowpower:
	$(MAKE) deploy PROJECT=greenhouse-node-lowpower PIO_ENV=lowpower_$(SLEEP)min

# Калібрувальний стенд ґрунтових сенсорів: збірка + заливка.
#   make deploy-soil-cal
#   make soil-log RUN=v12-vs-v20
# Логер тримає той самий порт, що й monitor — разом їх не запускати.
deploy-soil-cal:
	$(MAKE) deploy PROJECT=soil-calibration

soil-log:
	SERIAL_PORT=$(SERIAL_PORT) uv run --with pyserial \
		soil-calibration/tools/soil_log.py --run $(RUN)

# Живий монітор для етапу підключення дротів: по сенсору в рядок, зі шкалою
# і присудом "OK / ПЛАВАЄ / НУЛЬ". Той самий порт, що й soil-log — не разом.
soil-watch:
	SERIAL_PORT=$(SERIAL_PORT) uv run --with pyserial \
		soil-calibration/tools/soil_watch.py

# Підсумок останнього прогону; інший файл — `make soil-summary CSV=шлях`.
CSV ?= $(shell ls -t soil-calibration/data/*.csv 2>/dev/null | head -1)
soil-summary:
	@test -n "$(CSV)" || { echo "немає жодного CSV у soil-calibration/data/"; exit 1; }
	$(HOST_PYTHON) soil-calibration/tools/soil_summary.py $(CSV)

# gateway висить на USB Pi — заливка йде прямо там через SSH, не з Mac.
# Зупиняє agro-adapter (тримає той самий порт) перед upload і піднімає
# назад завжди, навіть якщо заливка впала. Деталі — firmware/DEPLOY.md.
deploy-gateway:
	rsync -az --exclude .pio gateway/ $(PI_HOST):$(PI_DIR)/firmware/gateway/
	ssh $(PI_HOST) 'set -e; \
		sudo systemctl stop agro-adapter; \
		trap "sudo systemctl start agro-adapter" EXIT; \
		cd $(PI_DIR)/firmware/gateway; \
		SERIAL_PORT=$(PI_GATEWAY_PORT) ~/.local/bin/uv tool run platformio run -t upload'

monitor: _ensure-host-pio
	SERIAL_PORT=$(SERIAL_PORT) pio device monitor -d $(PROJECT)

shell:
	docker compose run --rm --entrypoint sh $(PROJECT)

clean:
	docker compose run --rm $(PROJECT) run -t clean
