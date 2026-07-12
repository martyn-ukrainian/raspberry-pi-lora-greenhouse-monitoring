"""
Симулятор шлюза для тестування usb_adapter.py без реального заліза.
Пише NDJSON у /tmp/agro_gateway. socat перекидає у /tmp/agro_adapter,
звідки читає usb_adapter.
"""

import json
import random
import time

from simulate import INTERVAL_SECONDS

GATEWAY_PORT = "/tmp/agro_gateway"


def make_packet(node_id: int) -> dict:
    return {
        "type": "measurement",
        "node_id": node_id,
        "air_temperature": round(random.uniform(38, 42), 1),
        "air_humidity": round(random.uniform(50, 80), 1),
        "soil_moisture": round(random.uniform(30, 60), 1),
        "rssi": random.randint(-110, -70),
        "snr": round(random.uniform(-5, 10), 1),
    }


def run() -> None:
    with open(GATEWAY_PORT, "w", encoding="utf-8") as gw:
        print(f"Gateway simulator writing to {GATEWAY_PORT}")
        while True:
            node_id = random.choice([0, 1, 2])
            packet = make_packet(node_id)
            line = json.dumps(packet) + "\n"
            gw.write(line)
            gw.flush()
            print(f"sent: {line.strip()}")
            time.sleep(INTERVAL_SECONDS)


if __name__ == "__main__":
    try:
        run()
    except KeyboardInterrupt:
        print("\nGateway simulator stopped")
