"""
Логіка виявлення алертів для вимірів + анти-спам (dwell + cooldown).

Стан тримається в памʼяті. При рестарті сервера забувається — це прийнятно
для Фази 1. Опис у docs/alerts.md.
"""

# stdlib
from dataclasses import dataclass
from datetime import datetime, timedelta
from typing import Protocol

# third-party
from pydantic import BaseModel

# local
from logger import get_logger
from thresholds import SensorThresholds, thresholds

logger = get_logger(__name__)


class SensorReading(Protocol):
    """
    Структурний контракт вхідних даних для check().

    Будь-який об'єкт з цими полями годиться - SQLModel-таблиця Measurement,
    fake для тесту, dict.get-wrapper, тощо. alerts.py не має знати
    з якого сховища прийшли дані.
    """

    node_id: str
    air_temperature: float
    air_humidity: float
    soil_moisture: float
    timestamp: datetime


class Alert(BaseModel):
    node_id: str
    label: str
    sensor: str
    kind: str
    value: float
    boundary: float
    duration_minutes: int


@dataclass
class AlertState:
    out_since: datetime | None = None
    last_alert_at: datetime | None = None


_state: dict[tuple[str, str], AlertState] = {}


def _resolve_dwell(sensor: SensorThresholds) -> int:
    if sensor.dwell_minutes is not None:
        return sensor.dwell_minutes
    return thresholds.defaults.dwell_minutes


def _classify(
    value: float, sensor: SensorThresholds
) -> tuple[str | None, float | None]:
    """
    Повертає (kind, boundary): "high"/"low"/None + порушений поріг.
    None означає значення в нормі.
    """
    if sensor.max is not None and value > sensor.max:
        return "high", sensor.max
    if sensor.min is not None and value < sensor.min:
        return "low", sensor.min

    return None, None


def _get_state(node_id: str, sensor_name: str) -> AlertState:
    """Отримати state з пам'яті, створити якщо ще нема."""
    key = (node_id, sensor_name)
    if key not in _state:
        _state[key] = AlertState()
    return _state[key]


def _check_sensor(
    node_id: str,
    label: str,
    sensor_name: str,
    value: float,
    sensor_config: SensorThresholds,
    timestamp: datetime,
) -> Alert | None:
    kind, boundary = _classify(value, sensor_config)
    state = _get_state(node_id, sensor_name)

    # Все в нормі: скинути початок аномалії, cooldown лишається
    if kind is None:
        state.out_since = None
        return None

    # Аномалія тільки-но почалась - зафіксувати момент і чекати
    if state.out_since is None:
        state.out_since = timestamp
        return None

    # Витримка ще не пройшла
    duration = timestamp - state.out_since
    if duration < timedelta(minutes=_resolve_dwell(sensor_config)):
        return None

    # Cooldown після попереднього алерта ще діє
    cooldown = timedelta(minutes=thresholds.defaults.cooldown_minutes)
    if state.last_alert_at is not None and timestamp - state.last_alert_at < cooldown:
        return None

    # Всі перевірки пройшли - генеруємо алерт
    state.last_alert_at = timestamp
    return Alert(
        node_id=node_id,
        label=label,
        sensor=sensor_name,
        kind=kind,
        value=value,
        boundary=boundary,
        duration_minutes=int(duration.total_seconds() / 60),
    )


def check(measurement: SensorReading) -> list[Alert]:
    """
    Головна функція. Перевіряє вимір проти порогів своєї теплиці по трьох
    датчиках. Повертає список Alert-ів (0-3 залежно від того, скільки
    датчиків спрацювали).
    """
    greenhouse = thresholds.greenhouses.get(measurement.node_id)
    if greenhouse is None:
        logger.warning("No thresholds config for node %s", measurement.node_id)
        return []

    alerts: list[Alert] = []
    sensors = [
        ("air_temperature", greenhouse.air_temperature, measurement.air_temperature),
        ("air_humidity", greenhouse.air_humidity, measurement.air_humidity),
        ("soil_moisture", greenhouse.soil_moisture, measurement.soil_moisture),
    ]

    for sensor_name, sensor_config, value in sensors:
        alert = _check_sensor(
            node_id=measurement.node_id,
            label=greenhouse.label,
            sensor_name=sensor_name,
            value=value,
            sensor_config=sensor_config,
            timestamp=measurement.timestamp,
        )
        if alert is not None:
            alerts.append(alert)
    return alerts
