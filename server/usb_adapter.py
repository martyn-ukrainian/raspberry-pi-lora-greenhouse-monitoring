"""
USB-Serial адаптер: читає JSON-пакети від шлюза через USB, транслює
numeric node_id у string label і POST-ить на локальний сервер /measurements.

Формат вхідних JSON - GatewayPacket (описано нижче). Один пакет на рядок,
розділювач '\\n' (NDJSON). Формат розроблено разом з firmware, живе у
docs (планується).
"""

from pathlib import Path

import httpx
import serial
import yaml
from pydantic import BaseModel

from logger import get_logger

logger = get_logger(__name__)


# SERIAL_PORT = "/dev/tty.usbserial-XXX"
SERIAL_PORT = "/tmp/agro_adapter"
BAUDRATE = 115200
SERVER_URL = "http://127.0.0.1:8000"
NODES_CONFIG_PATH = Path(__file__).parent / "config" / "nodes.yaml"


class GatewayPacket(BaseModel):
    """
    Форма одного JSON-пакета зі шлюза через USB-Serial.
    Погоджений з firmware - див. docs (планується).
    """

    type: str
    node_id: int
    air_temperature: float
    air_humidity: float
    soil_moisture: float
    rssi: int
    snr: float


def load_nodes() -> dict[int, str]:
    with NODES_CONFIG_PATH.open(encoding="utf-8") as f:
        data = yaml.safe_load(f)
    nodes = data["nodes"]
    logger.info("Loaded %d nodes from %s", len(nodes), NODES_CONFIG_PATH.name)
    return nodes


def run() -> None:
    nodes = load_nodes()

    with (
        serial.Serial(SERIAL_PORT, BAUDRATE, timeout=1) as ser,
        httpx.Client(base_url=SERVER_URL) as client,
    ):
        logger.info("USB adapter started on %s at %d baud", SERIAL_PORT, BAUDRATE)

        while True:
            line = ser.readline().decode("utf-8").strip()
            if not line:
                continue

            try:
                packet = GatewayPacket.model_validate_json(line)
            except Exception as exc:
                logger.warning(
                    "Skipping malformed packet: %s (error: %s)", line[:80], exc
                )
                continue

            if packet.type != "measurement":
                logger.debug("Ignoring non-measurement packet type=%s", packet.type)
                continue

            label = nodes.get(packet.node_id)
            if label is None:
                logger.warning(
                    "Unknown node_id=%d, add it to nodes.yaml", packet.node_id
                )
                continue

            body = {
                "node_id": label,
                "air_temperature": packet.air_temperature,
                "air_humidity": packet.air_humidity,
                "soil_moisture": packet.soil_moisture,
            }

            try:
                response = client.post("/measurements", json=body)
                response.raise_for_status()
                logger.info(
                    "Forwarded %s: temp=%.1f humidity=%.1f soil=%f.1f rssi=%d snr=%.1f",
                    label,
                    packet.air_temperature,
                    packet.air_humidity,
                    packet.soil_moisture,
                    packet.rssi,
                    packet.snr,
                )
            except httpx.HTTPError as exc:
                logger.error("Failed to POST to server: %s", exc)


if __name__ == "__main__":
    try:
        run()
    except KeyboardInterrupt:
        logger.info("USB adapter stopped")
