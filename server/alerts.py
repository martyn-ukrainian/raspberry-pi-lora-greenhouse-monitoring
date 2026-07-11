"""
Логіка виявлення алертів для вимірів + анти-спам (dwell + cooldown).

Стан тримається в памʼяті. При рестарті сервера забувається — це прийнятно
для Фази 1. Опис у docs/alerts.md.
"""

# stdlib
from dataclasses import dataclass
from datetime import datetime, timedelta

# third-party
from pydantic import BaseModel

# local
from logger import get_logger
from thresholds import SensorThresholds, thresholds

logger = get_logger(__name__)


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

    #  Все в нормі: скинути початок аномалії, cooldown лишається
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
