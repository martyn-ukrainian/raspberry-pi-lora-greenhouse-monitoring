from datetime import UTC, datetime
from typing import Annotated

from fastapi import APIRouter, Depends
from sqlmodel import Field, Session, SQLModel, select

from alerts import check
from database import engine
from logger import get_logger
from notifiers import get_notifier
from notifiers.base import Notifier

logger = get_logger(__name__)


class Measurement(SQLModel, table=True):
    id: int | None = Field(default=None, primary_key=True)
    node_id: str
    air_temperature: float
    air_humidity: float
    soil_moisture: float
    timestamp: datetime = Field(default_factory=lambda: datetime.now(UTC))


class MeasurementRepository:
    def __init__(self, engine):
        self.engine = engine

    def create(self, measurement: Measurement) -> Measurement:
        with Session(self.engine) as session:
            session.add(measurement)
            session.commit()
            session.refresh(measurement)
        return measurement

    def list_all(self) -> list[Measurement]:
        with Session(self.engine) as session:
            return session.exec(select(Measurement)).all()


def get_repository() -> MeasurementRepository:
    return MeasurementRepository(engine)


router = APIRouter(prefix="/measurements", tags=["measurements"])

RepositoryDep = Annotated[MeasurementRepository, Depends(get_repository)]
NotifierDep = Annotated[Notifier, Depends(get_notifier)]


@router.post("")
def create_measurement(
    measurement: Measurement, repo: RepositoryDep, notifier: NotifierDep
) -> Measurement:
    logger.info("Received measurement from node %s", measurement.node_id)
    saved = repo.create(measurement)
    logger.info("Saved measurement id=%d for node %s", saved.id, saved.node_id)

    for alert in check(saved):
        notifier.send(alert)

    return saved


@router.get("")
def read_measurement(repo: RepositoryDep) -> list[Measurement]:
    measurements = repo.list_all()
    logger.info("Returned %d measurements", len(measurements))
    return measurements
