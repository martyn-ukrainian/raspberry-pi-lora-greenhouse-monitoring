"""
Логіка виявлення алертів для вимірів + анти-спам (dwell + cooldown).

Стан тримається в памʼяті. При рестарті сервера забувається — це прийнятно
для Фази 1. Опис у docs/alerts.md.
"""

# stdlib
from dataclasses import dataclass
from datetime import UTC, datetime, timedelta
from typing import Annotated, Protocol

from fastapi import APIRouter, Depends, Query
from pydantic import BaseModel, field_serializer
from sqlmodel import Field, Session, SQLModel, col, select

# local
from database import engine
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


class StoredAlert(SQLModel, table=True):  # type: ignore[call-arg]
    id: int | None = Field(default=None, primary_key=True)
    node_id: str
    label: str
    sensor: str
    kind: str
    value: float
    boundary: float
    duration_minutes: int
    created_at: datetime = Field(default_factory=lambda: datetime.now(UTC))
    acknowledged: bool = Field(default=False)

    # SQLite drops tzinfo on write. Force UTC on the wire so the client can
    # trust the timezone contract and just localize for display.
    @field_serializer("created_at")
    def _serialize_created_at(self, v: datetime) -> str:
        if v.tzinfo is None:
            v = v.replace(tzinfo=UTC)
        return v.isoformat()


class AlertRepository:
    def __init__(self, engine):
        self.engine = engine

    def create(self, alert: Alert) -> StoredAlert:
        with Session(self.engine) as session:
            stored = StoredAlert(**alert.model_dump())
            session.add(stored)
            session.commit()
            session.refresh(stored)
        return stored

    def list_recent(self, limit: int = 50) -> list[StoredAlert]:
        with Session(self.engine) as session:
            stmt = (
                select(StoredAlert)
                .order_by(col(StoredAlert.created_at).desc())
                .limit(limit)
            )

            return list(session.exec(stmt).all())

    def ack_all(self) -> int:
        """Позначити всі непрочитані як прочитані. Повертає скільки оновлено."""
        with Session(self.engine) as session:
            stmt = select(StoredAlert).where(col(StoredAlert.acknowledged).is_(False))
            alerts = session.exec(stmt).all()
            for a in alerts:
                a.acknowledged = True
            session.commit()
            return len(alerts)


def get_repository() -> AlertRepository:
    return AlertRepository(engine)


AlertRepositoryDep = Annotated[AlertRepository, Depends(get_repository)]

router = APIRouter(prefix="/alerts", tags=["alerts"])


@router.get("")
def read_alerts(
    repo: AlertRepositoryDep,
    limit: Annotated[int, Query(ge=1, le=200)] = 50,
) -> list[StoredAlert]:
    alerts = repo.list_recent(limit)
    logger.info("Returned %d alerts (limit=%d)", len(alerts), limit)

    return alerts


@router.post("/ack-all")
def ack_all_alerts(repo: AlertRepositoryDep) -> dict[str, int]:
    count = repo.ack_all()
    logger.info("Acknowledged %d alerts", count)
    return {"acknowledged": count}
