"""
Симулятор шлюза для тестування usb_adapter.py без реального заліза.
Пише NDJSON у /tmp/agro_gateway. socat перекидає у /tmp/agro_adapter,
звідки читає usb_adapter.

Крім здорових вимірів, з невеликою ймовірністю підкидає те, що шле справжнє
залізо, коли ламається: бітмаск `err` від вузла, пакет без полів повітря
(сенсор мовчить) і власну подію шлюза. Без цього шлях помилок можна перевірити
тільки чекаючи реальної поломки в теплиці.
"""

import json
import random
import time

from settings import settings
from simulate import INTERVAL_SECONDS

GATEWAY_PORT = settings.gateway_port


# Наскільки часто симулювати збій. У теплиці події рідкісні — тут теж, щоб
# лог не перетворився на суцільні помилки, але й не чекати годину.
FAULT_PROBABILITY = 0.1

# EV_* з firmware/greenhouse-node-lowpower/src/main.cpp.
EV_RST_BROWNOUT = 2
EV_LORA_TX_FAIL = 6
EV_SHT_NAN = 8
EV_BATTERY_LOW = 10


def make_packet(node_id: int, boot: int) -> dict:
    packet = {
        "type": "measurement",
        "node_id": node_id,
        "air_temperature": round(random.uniform(20, 30), 1),
        "air_humidity": round(random.uniform(50, 80), 1),
        "soil_moisture": round(random.uniform(30, 60), 1),
        "vbat": round(random.uniform(3.6, 4.1), 2),
        "boot": boot,
        "rssi": random.randint(-110, -70),
        "snr": round(random.uniform(-5, 10), 1),
    }

    if random.random() >= FAULT_PROBABILITY:
        return packet

    fault = random.choice(
        [EV_RST_BROWNOUT, EV_LORA_TX_FAIL, EV_SHT_NAN, EV_BATTERY_LOW]
    )
    packet["err"] = 1 << fault
    packet["eseq"] = boot

    if fault == EV_SHT_NAN:
        # Сенсор повітря мовчить — прошивка не підставляє нулі, а прибирає поля.
        del packet["air_temperature"]
        del packet["air_humidity"]

    return packet


def make_gateway_event() -> dict:
    return {
        "type": "event",
        "node_id": 255,
        "code": 3,  # GWEV_CRC_BURST
        "detail": random.randint(10, 200),
    }


def run() -> None:
    boot = 0

    with open(GATEWAY_PORT, "w", encoding="utf-8") as gw:
        print(f"Gateway simulator writing to {GATEWAY_PORT}")
        while True:
            boot += 1
            node_id = random.choice([0, 1, 2])

            if random.random() < FAULT_PROBABILITY:
                packet = make_gateway_event()
            else:
                packet = make_packet(node_id, boot)

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
