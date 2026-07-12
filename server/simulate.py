"""
Симулятор трьох сенсорних вузлів у теплицях.
Шле реалістичні виміри на локальний сервер кожні кілька секунд.

Запуск (у другому терміналі, поки сервер крутиться):
    uv run python simulate.py

Зупинка: Ctrl+C.
"""

import random
import time
from datetime import UTC, datetime

import httpx

SERVER_URL = "http://127.0.0.1:8000"
NODES = ["greenhouse-1", "greenhouse-2", "greenhouse-3"]
INTERVAL_SECONDS = 5


def make_measurement(node_id: str) -> dict:
    return {
        "node_id": node_id,
        "air_temperature": round(random.uniform(38, 42), 1),
        "air_humidity": round(random.uniform(50, 80), 1),
        "soil_moisture": round(random.uniform(30, 60), 1),
    }


def run() -> None:
    with httpx.Client(base_url=SERVER_URL) as client:
        while True:
            node = random.choice(NODES)
            payload = make_measurement(node)
            try:
                response = client.post("/measurements", json=payload)
                now = datetime.now(UTC).isoformat(timespec="seconds")
                print(f"[{now}] {node} -> {response.status_code}: {payload}")
            except httpx.RequestError as e:
                print(f"connection error: {e}")
            time.sleep(INTERVAL_SECONDS)


if __name__ == "__main__":
    run()
