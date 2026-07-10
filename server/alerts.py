"""
Логіка виявлення алертів для вимірів + анти-спам (dwell + cooldown).

Стан тримається в памʼяті. При рестарті сервера забувається — це прийнятно
для Фази 1. Опис у docs/alerts.md.
"""

# stdlib
from dataclasses import dataclass
from datetime import datetime

# third-party
from pydantic import BaseModel

# local
from logger import get_logger

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
