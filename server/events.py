"""
Журнал подій заліза: коди помилок від вузлів і шлюза.

Це не заміна `logger.py` — там текстовий лог самого серверу. Тут те, що
прийшло з плати: вузол не має куди писати лог (Serial у теплиці нікуди не
під'єднаний, flash при 15-хвилинному циклі дорожчий за сам вимір), тому він
тримає події в 64 байтах RTC-пам'яті й доносить їх бітмаском у полі `err`
чергового пакета. Архів — тут.

Утримання: EVENT_RETENTION_DAYS. Старіші рядки прибираються при вставці нових
(подій одиниці на тиждень, тож окремий cron надлишковий).
"""

from datetime import UTC, datetime, timedelta
from typing import Annotated

from fastapi import APIRouter, Depends, Query
from pydantic import BaseModel, field_serializer
from sqlmodel import Field, Session, SQLModel, col, delete, select

from database import engine
from logger import get_logger

logger = get_logger(__name__)

EVENT_RETENTION_DAYS = 14

# Джерело події. Коди в цих двох просторах НЕ спільні: у вузла свої причини
# збоїв, у шлюза свої, і зливати їх в одну нумерацію означало б плутати
# "сенсор не відповідає" з "радіо не піднялось".
SOURCE_NODE = "node"
SOURCE_GATEWAY = "gateway"

# Дзеркало EV_* з firmware/greenhouse-node-lowpower/src/main.cpp.
# Міняти синхронно з прошивкою — вузол шле число, розшифровка живе тільки тут.
NODE_EVENT_NAMES = {
    1: "cold_boot",
    2: "reset_brownout",
    3: "reset_panic",
    4: "reset_watchdog",
    5: "lora_init_failed",
    6: "lora_tx_failed",
    7: "i2c_sensor_missing",
    8: "air_sensor_nan",
    9: "soil_sensor_out_of_range",
    10: "battery_low",
    11: "battery_critical",
}

# Дзеркало GWEV_* з firmware/gateway/src/main.cpp.
GATEWAY_EVENT_NAMES = {
    1: "lora_init_failed",
    2: "rx_failed",
    3: "crc_error_burst",
    # detail = esp_reset_reason(). Шлюз живиться від Pi і мовчки
    # перезавантажується; без цієї події рестарт видно лише як провал у даних.
    4: "boot",
    # detail = скільки рядків не доїхало. Тільки в мережевому режимі: поки
    # сокет лежить, шлюз тримає чергу, і при переповненні витісняє найстаріше.
    # Без цієї події втрата виглядала б на сервері як тиша в ефірі, тобто як
    # несправність вузла — а причина була в мережі.
    5: "network_backlog_dropped",
}


def event_name(source: str, code: int) -> str:
    names = GATEWAY_EVENT_NAMES if source == SOURCE_GATEWAY else NODE_EVENT_NAMES
    return names.get(code, f"unknown_{code}")


def decode_flags(err_flags: int) -> list[int]:
    """
    Бітмаск `err` з пакета -> список кодів подій.

    Вузол ставить біт `1 << code` і не гасить його, доки передача не пройшла
    вдало, тому в одному пакеті може приїхати кілька подій одразу — саме так
    доїжджає те, що сталось під час невдалих спроб.
    """
    return [code for code in range(1, 32) if err_flags & (1 << code)]


class NodeEvent(SQLModel, table=True):  # type: ignore[call-arg]
    id: int | None = Field(default=None, primary_key=True)
    node_id: str
    source: str = Field(default=SOURCE_NODE)
    code: int
    name: str
    # Номер прокидання вузла (`boot` у пакеті) або `detail` шлюза — залежно
    # від джерела. Дозволяє відрізнити "сталось один раз давно" від "сиплеться
    # щоцикл", не покладаючись на час приходу.
    context: int | None = Field(default=None)
    timestamp: datetime = Field(default_factory=lambda: datetime.now(UTC), index=True)

    @field_serializer("timestamp")
    def _serialize_timestamp(self, v: datetime) -> str:
        if v.tzinfo is None:
            v = v.replace(tzinfo=UTC)
        return v.isoformat()


class EventCreate(BaseModel):
    node_id: str
    source: str = SOURCE_NODE
    code: int
    context: int | None = None


class EventRepository:
    def __init__(self, engine):
        self.engine = engine

    def create(self, event: EventCreate) -> NodeEvent:
        stored = NodeEvent(
            node_id=event.node_id,
            source=event.source,
            code=event.code,
            name=event_name(event.source, event.code),
            context=event.context,
        )
        with Session(self.engine) as session:
            session.add(stored)
            session.commit()
            session.refresh(stored)

        self.purge_expired()
        return stored

    def purge_expired(self) -> int:
        """Прибрати рядки, старші за EVENT_RETENTION_DAYS. Повертає скільки."""
        cutoff = datetime.now(UTC) - timedelta(days=EVENT_RETENTION_DAYS)
        with Session(self.engine) as session:
            stmt = delete(NodeEvent).where(col(NodeEvent.timestamp) < cutoff)
            removed = session.exec(stmt).rowcount  # type: ignore[call-overload]
            session.commit()

        if removed:
            logger.info(
                "Purged %d events older than %d days", removed, EVENT_RETENTION_DAYS
            )
        return removed or 0

    def list_recent(self, limit: int = 50) -> list[NodeEvent]:
        with Session(self.engine) as session:
            stmt = (
                select(NodeEvent).order_by(col(NodeEvent.timestamp).desc()).limit(limit)
            )
            return list(session.exec(stmt).all())


def get_repository() -> EventRepository:
    return EventRepository(engine)


EventRepositoryDep = Annotated[EventRepository, Depends(get_repository)]

router = APIRouter(prefix="/events", tags=["events"])


@router.post("")
def create_event(event: EventCreate, repo: EventRepositoryDep) -> NodeEvent:
    stored = repo.create(event)
    logger.warning(
        "Hardware event: node=%s source=%s code=%d (%s) context=%s",
        stored.node_id,
        stored.source,
        stored.code,
        stored.name,
        stored.context,
    )
    return stored


@router.get("")
def read_events(
    repo: EventRepositoryDep,
    limit: Annotated[int, Query(ge=1, le=200)] = 50,
) -> list[NodeEvent]:
    events = repo.list_recent(limit)
    logger.info("Returned %d events (limit=%d)", len(events), limit)
    return events
