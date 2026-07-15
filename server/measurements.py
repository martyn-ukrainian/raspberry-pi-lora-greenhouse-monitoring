from datetime import UTC, datetime, timedelta
from typing import Annotated

from fastapi import APIRouter, Depends, Query
from pydantic import BaseModel, field_serializer
from sqlalchemy import Integer
from sqlmodel import Field, Session, SQLModel, func, select

from alerts import AlertRepositoryDep, check
from database import engine
from logger import get_logger
from notifiers import get_notifier
from notifiers.base import Notifier

logger = get_logger(__name__)


class Measurement(SQLModel, table=True):  # type: ignore[call-arg]
    id: int | None = Field(default=None, primary_key=True)
    node_id: str
    air_temperature: float
    air_humidity: float
    soil_moisture: float
    timestamp: datetime = Field(default_factory=lambda: datetime.now(UTC))

    @field_serializer("timestamp")
    def _serialize_timestamp(self, v: datetime) -> str:
        if v.tzinfo is None:
            v = v.replace(tzinfo=UTC)
        return v.isoformat()


class SensorStats(BaseModel):
    min: float
    max: float
    avg: float


class AggregateBucket(BaseModel):
    bucket: datetime
    count: int
    air_temperature: SensorStats
    air_humidity: SensorStats
    soil_moisture: SensorStats

    @field_serializer("bucket")
    def _serialize_bucket(self, v: datetime) -> str:
        if v.tzinfo is None:
            v = v.replace(tzinfo=UTC)
        return v.isoformat()


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

    def list_aggregate(
        self,
        node_id: str,
        since: datetime,
        bucket_seconds: int,
    ) -> list[dict]:
        unix_ts = func.cast(func.strftime("%s", Measurement.timestamp), Integer)
        bucket = func.datetime(
            unix_ts - (unix_ts % bucket_seconds),
            "unixepoch",
        ).label("bucket")

        with Session(self.engine) as session:
            stmt = (
                select(
                    bucket,
                    func.count().label("count"),
                    func.min(Measurement.air_temperature).label("t_min"),
                    func.max(Measurement.air_temperature).label("t_max"),
                    func.avg(Measurement.air_temperature).label("t_avg"),
                    func.min(Measurement.air_humidity).label("h_min"),
                    func.max(Measurement.air_humidity).label("h_max"),
                    func.avg(Measurement.air_humidity).label("h_avg"),
                    func.min(Measurement.soil_moisture).label("m_min"),
                    func.max(Measurement.soil_moisture).label("m_max"),
                    func.avg(Measurement.soil_moisture).label("m_avg"),
                )
                .where(Measurement.node_id == node_id)
                .where(Measurement.timestamp >= since)
                .group_by(bucket)
                .order_by(bucket)
            )
            return [row._asdict() for row in session.exec(stmt).all()]


def get_repository() -> MeasurementRepository:
    return MeasurementRepository(engine)


router = APIRouter(prefix="/measurements", tags=["measurements"])

RepositoryDep = Annotated[MeasurementRepository, Depends(get_repository)]
NotifierDep = Annotated[Notifier, Depends(get_notifier)]


@router.post("")
def create_measurement(
    measurement: Measurement,
    repo: RepositoryDep,
    notifier: NotifierDep,
    alert_repo: AlertRepositoryDep,
) -> Measurement:
    logger.info("Received measurement from node %s", measurement.node_id)
    saved = repo.create(measurement)
    logger.info("Saved measurement id=%d for node %s", saved.id, saved.node_id)

    for alert in check(saved):
        notifier.send(alert)
        alert_repo.create(alert)
        logger.info("Sotored alert: %s %s %s", alert.node_id, alert.sensor, alert.kind)

    return saved


@router.get("")
def read_measurement(repo: RepositoryDep) -> list[Measurement]:
    measurements = repo.list_all()
    logger.info("Returned %d measurements", len(measurements))
    return measurements


@router.get("/aggregate")
def read_aggregate(
    node_id: str,
    repo: RepositoryDep,
    since: Annotated[datetime | None, Query()] = None,
    bucket_minutes: Annotated[int, Query(ge=1, le=1440)] = 5,
) -> list[AggregateBucket]:
    if since is None:
        since = datetime.now(UTC) - timedelta(hours=4)

    # TODO: optionally snap `since` down to bucket boundary so the first
    # bucket is always full instead of partial:
    #     bucket_seconds = bucket_minutes * 60
    #     epoch = int(since.timestamp())
    #     since = datetime.fromtimestamp(epoch - epoch % bucket_seconds, tz=UTC)
    # Trade-off: actual range grows by up to bucket_seconds.
    rows = repo.list_aggregate(
        node_id=node_id,
        since=since,
        bucket_seconds=bucket_minutes * 60,
    )

    logger.info(
        "Aggregated %d buckets for node %s (since=%s, bucket=%dm)",
        len(rows),
        node_id,
        since.isoformat(),
        bucket_minutes,
    )

    return [
        AggregateBucket(
            bucket=row["bucket"],
            count=row["count"],
            air_temperature=SensorStats(
                min=row["t_min"], max=row["t_max"], avg=row["t_avg"]
            ),
            air_humidity=SensorStats(
                min=row["h_min"], max=row["h_max"], avg=row["h_avg"]
            ),
            soil_moisture=SensorStats(
                min=row["m_min"], max=row["m_max"], avg=row["m_avg"]
            ),
        )
        for row in rows
    ]
