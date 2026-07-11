"""
Реалізація Notifier для Telegram Bot API.

Прямий бот, створений @BotFather. Token і chat_id читаються з .env
через модуль settings. Кожен алерт формується як HTML-повідомлення і
надсилається через httpx-клієнт.
"""

import httpx

from alerts import Alert
from logger import get_logger
from messages import DIRECTIONS, ICONS, SENSOR_NAMES, UNITS

logger = get_logger(__name__)


class TelegramNotifier:
    def __init__(self, token: str, chat_id: str) -> None:
        self._token = token
        self._chat_id = chat_id
        self._url = f"https://api.telegram.org/bot{token}/sendMessage"
        self._client = httpx.Client(timeout=5.0)

    def send(self, alert: Alert) -> None:
        text = self._format(alert)
        try:
            response = self._client.post(
                self._url,
                json={
                    "chat_id": self._chat_id,
                    "text": text,
                    "parse_mode": "HTML",
                },
            )
            response.raise_for_status()
            logger.info(
                "Alert sent to Telegram: node=%s sensor=%s kind=%s",
                alert.node_id,
                alert.sensor,
                alert.kind,
            )
        except httpx.HTTPError as exc:
            logger.error("failed to send Telegram alert: %s", exc)

    def _format(self, alert: Alert) -> str:
        icon = ICONS.get(alert.sensor, {}).get(alert.kind, ICONS["fallback"])
        sensor_name = SENSOR_NAMES.get(alert.sensor, alert.sensor)
        direction = DIRECTIONS.get(alert.kind, "поза межами")
        unit = UNITS.get(alert.sensor, "")

        return (
            f"{icon} <b>Теплиця «{alert.label}»</b>\n"
            f"{sensor_name}: {alert.value}{unit} - {direction} норми "
            f"(поріг {alert.boundary}{unit})\n"
            f"Триває вже {alert.duration_minutes} хв"
        )
