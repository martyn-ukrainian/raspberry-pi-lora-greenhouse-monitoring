"""
Адаптер шлюза: читає JSON-пакети, транслює numeric node_id у string label
і POST-ить на локальний сервер.

Джерел два, і вибір між ними — це `GATEWAY_SOURCE` у `.env`:

    serial — плата на USB Pi (як було з самого початку).
    tcp    — плата сама підключається до нас по Wi-Fi і шле те саме в сокет.

Другий режим існує не з любові до мережі: шнур, яким шлюз під'єднаний до
малини, виявився зарядним — ліній даних у ньому нема, `lsusb` порожній, хоча
плата живиться. Замість шукати інший кабель прошивка дублює потік у TCP.

Свідомо змінюється ТІЛЬКИ джерело байтів. Розбір NDJSON, мапінг номерів у
мітки й розкладання `err` лишаються тут, спільні для обох трактів: перенести
їх у C++ означало б тримати дві реалізації одного контракту.

Формат вхідних JSON — один пакет на рядок, розділювач '\\n' (NDJSON).
Два типи, обидва погоджені з firmware:

    type="measurement" — вимір від вузла (+ rssi/snr, дописані шлюзом).
        Поля air_* можуть бути ВІДСУТНІ: так вузол каже "сенсор не відповів".
        Це навмисно не нуль — нуль сервер записав би як справжні -0 °C.
    type="event"       — код помилки від самого шлюза.

Крім того, у пакеті виміру може приїхати `err` — бітмаск подій, які вузол
накопичив у RTC-пам'яті з моменту останньої вдалої передачі. Розкладаємо
його на окремі коди й пишемо в /events.
"""

import json
import socket
from collections.abc import Iterator
from pathlib import Path

import httpx
import serial
import yaml
from pydantic import BaseModel

from events import SOURCE_GATEWAY, SOURCE_NODE, decode_flags
from logger import get_logger
from settings import settings

logger = get_logger(__name__)


SERIAL_PORT = settings.serial_port
BAUDRATE = settings.baudrate
SERVER_URL = settings.server_url
NODES_CONFIG_PATH = Path(__file__).parent / "config" / "nodes.yaml"

GATEWAY_SOURCE = settings.gateway_source
TCP_HOST = settings.gateway_tcp_host
TCP_PORT = settings.gateway_tcp_port


class GatewayPacket(BaseModel):
    """
    Форма одного JSON-пакета зі шлюза через USB-Serial.
    Погоджений з firmware - див. docs (планується).
    """

    type: str
    node_id: int
    # None = вузол не зміг прочитати сенсор повітря. Причина приїде в `err`.
    air_temperature: float | None = None
    air_humidity: float | None = None
    soil_moisture: float
    # Сире ADC. Шлють обидві бойові прошивки; None лише від старих збірок.
    soil_raw: int | None = None
    rssi: int
    snr: float
    # vbat шлють обидві прошивки (у greenhouse-node доданий для етапу 3),
    # boot — тільки -lowpower, uptime — тільки безперервний вузол.
    vbat: float | None = None
    boot: int | None = None
    uptime: int | None = None
    err: int = 0
    eseq: int | None = None


class EventPacket(BaseModel):
    """Подія від самого шлюза (node_id=255), не від вузла."""

    type: str
    node_id: int
    code: int
    detail: int | None = None


def load_nodes() -> dict[int, str]:
    with NODES_CONFIG_PATH.open(encoding="utf-8") as f:
        data = yaml.safe_load(f)
    nodes = data["nodes"]
    logger.info("Loaded %d nodes from %s", len(nodes), NODES_CONFIG_PATH.name)
    return nodes


def post_event(
    client: httpx.Client, label: str, source: str, code: int, context: int | None
) -> None:
    """
    Подія не має ламати потік вимірів: якщо сервер відмовив, пишемо в лог і
    йдемо далі. Втратити код помилки прикро, втратити вимір — гірше.
    """
    body = {"node_id": label, "source": source, "code": code, "context": context}
    try:
        client.post("/events", json=body).raise_for_status()
        logger.warning("Event from %s: code=%d context=%s", label, code, context)
    except httpx.HTTPError as exc:
        logger.error("Failed to POST event: %s", exc)


def handle_measurement(client: httpx.Client, packet: GatewayPacket, label: str) -> None:
    # Спершу події: якщо вимір далі відсіється, причина вже збережена.
    for code in decode_flags(packet.err):
        post_event(client, label, SOURCE_NODE, code, packet.boot)

    if packet.air_temperature is None or packet.air_humidity is None:
        # Ґрунт у пакеті є, але схема Measurement вимагає повітря non-null,
        # тож цей цикл втрачаємо цілком. Подія вище вже пояснює чому.
        logger.warning("Dropping measurement from %s: air sensor gave no data", label)
        return

    body = {
        "node_id": label,
        "air_temperature": packet.air_temperature,
        "air_humidity": packet.air_humidity,
        "soil_moisture": packet.soil_moisture,
        "soil_raw": packet.soil_raw,
        "rssi": packet.rssi,
        "snr": packet.snr,
        "vbat": packet.vbat,
        "uptime": packet.uptime,
    }

    try:
        response = client.post("/measurements", json=body)
        response.raise_for_status()
        logger.info(
            "Forwarded %s: temp=%.1f humidity=%.1f soil=%.1f rssi=%d snr=%.1f",
            label,
            packet.air_temperature,
            packet.air_humidity,
            packet.soil_moisture,
            packet.rssi,
            packet.snr,
        )
    except httpx.HTTPError as exc:
        logger.error("Failed to POST to server: %s", exc)


def handle_line(client: httpx.Client, nodes: dict[int, str], line: str) -> None:
    # Тип читаємо до валідації: measurement і event мають різні обов'язкові
    # поля, тож одна модель на обидва або пропускала б помилки, або відсівала
    # правильні пакети.
    try:
        raw = json.loads(line)
        packet_type = raw["type"]
    except (json.JSONDecodeError, KeyError, TypeError) as exc:
        logger.warning("Skipping malformed packet: %s (error: %s)", line[:80], exc)
        return

    if packet_type not in ("measurement", "event"):
        logger.debug("Ignoring unknown packet type=%s", packet_type)
        return

    try:
        packet: GatewayPacket | EventPacket = (
            GatewayPacket.model_validate(raw)
            if packet_type == "measurement"
            else EventPacket.model_validate(raw)
        )
    except Exception as exc:
        logger.warning("Skipping malformed packet: %s (error: %s)", line[:80], exc)
        return

    label = nodes.get(packet.node_id)
    if label is None:
        logger.warning("Unknown node_id=%d, add it to nodes.yaml", packet.node_id)
        return

    if isinstance(packet, EventPacket):
        post_event(client, label, SOURCE_GATEWAY, packet.code, packet.detail)
    else:
        handle_measurement(client, packet, label)


def iter_serial_lines() -> Iterator[str]:
    with serial.Serial(SERIAL_PORT, BAUDRATE, timeout=1) as ser:
        logger.info("Reading gateway from serial %s at %d baud", SERIAL_PORT, BAUDRATE)
        while True:
            yield ser.readline().decode("utf-8", errors="replace").strip()


def enable_keepalive(conn: socket.socket) -> None:
    """
    Без keepalive обрив живлення шлюза не помітний: TCP не має чого надіслати,
    тож сокет лишається "відкритим" вічно, а адаптер сидить у read() і не
    приймає нове підключення від плати, яка вже перезавантажилась.

    Штатна тиша тут звичне явище (сплячий вузол мовчить 15 хв), тому таймаут на
    читання не годиться — він відрізав би здорове з'єднання. Keepalive же
    перевіряє саме канал, а не потік даних: ~90 с після зникнення плати.
    """
    conn.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
    tuning = (("TCP_KEEPIDLE", 60), ("TCP_KEEPINTVL", 10), ("TCP_KEEPCNT", 3))
    for name, value in tuning:
        # TCP_KEEPIDLE є в Linux (тобто на Pi), у macOS воно зветься інакше —
        # тому не падаємо, а вдовольняємось дефолтами системи.
        option = getattr(socket, name, None)
        if option is not None:
            conn.setsockopt(socket.IPPROTO_TCP, option, value)


def iter_tcp_lines() -> Iterator[str]:
    with socket.create_server((TCP_HOST, TCP_PORT)) as server:
        logger.info("Waiting for gateway on tcp://%s:%d", TCP_HOST, TCP_PORT)

        while True:
            conn, address = server.accept()
            logger.info("Gateway connected from %s", address[0])
            enable_keepalive(conn)

            try:
                with conn, conn.makefile("r", encoding="utf-8", errors="replace") as f:
                    for line in f:
                        yield line.strip()
            except OSError as exc:
                logger.warning("Gateway connection lost: %s", exc)

            # Не виходимо: плата перепідключиться сама, і єдине, що має статись
            # при обриві — пауза в даних, а не смерть сервісу.
            logger.info("Gateway disconnected, waiting for it to come back")


def open_source() -> Iterator[str]:
    if GATEWAY_SOURCE == "tcp":
        return iter_tcp_lines()
    if GATEWAY_SOURCE == "serial":
        return iter_serial_lines()
    raise ValueError(
        f"GATEWAY_SOURCE={GATEWAY_SOURCE!r}: очікується 'serial' або 'tcp'"
    )


def run() -> None:
    nodes = load_nodes()

    with httpx.Client(base_url=SERVER_URL) as client:
        for line in open_source():
            if not line:
                continue
            handle_line(client, nodes, line)


if __name__ == "__main__":
    try:
        run()
    except KeyboardInterrupt:
        logger.info("USB adapter stopped")
