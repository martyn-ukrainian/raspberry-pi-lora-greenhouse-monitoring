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
    # Nullable, бо "сенсор не відповів" — це не нуль. Вузол у такому разі не
    # шле полів зовсім, і доти ці колонки були NOT NULL, через що adapter
    # відкидав пакет ЦІЛКОМ: поломка датчика повітря знищувала й вимір ґрунту,
    # який був справним. Див. міграцію c4f1a8e37b02.
    air_temperature: float | None = Field(default=None)
    air_humidity: float | None = Field(default=None)
    soil_moisture: float
    # Сире ADC ґрунтового сенсора, 0..4095.
    #
    # Зберігається поруч із відсотком навмисно: відсоток — це ТЛУМАЧЕННЯ, яке
    # залежить від калібрування, а сире значення — сам ВИМІР. Поки робочий
    # діапазон грядки невідомий (заміряно "щойно полито", але не "пора
    # поливати"), шкала ще зміниться — і тоді історію можна буде перерахувати
    # без перепрошивки вузлів у полі.
    #
    # Без цього поля кожна зміна калібрування розривала б історію надвоє:
    # дані до і після опинялись би в різних одиницях.
    soil_raw: int | None = Field(default=None)
    # Скільки мілісекунд від подачі Vext минуло до заміру ґрунту.
    #
    # Прошивка `-lowpower` везе це поле з коміту 1359c02, але жоден шар
    # приймача його не оголошував, тож pydantic мовчки викидав його — рівно
    # так, як до цього губився `vbat` (див. коментар нижче). Наслідок: вікно
    # семплювання їздило разом із міткою часу, а в базу лягав лише вимір, і
    # питання "за скільки сенсор виходить на полицю" лишалось невимірюваним.
    #
    # Без цього поля позицію відліку доводиться відновлювати з різниць
    # timestamp, а вони знімаються на приймальному боці й несуть час ефіру,
    # а не час заміру.
    soil_at_ms: int | None = Field(default=None)
    # Розкид відліків усередині бурста. Медіана вже лежить у soil_raw; ці два
    # поля кажуть, наскільки їй можна вірити в ЦЬОМУ циклі.
    #
    # Без них розкид доводиться рахувати за добу, а на такому вікні він міряє
    # вже не шум сенсора, а реальний рух ґрунту й температури — тобто зовсім
    # іншу величину.
    #
    # Заразом це єдиний спосіб перевірити, чи незалежні відліки через
    # SOIL_GAP_MS=50: якщо розмах помітно вужчий за той, що v1 давала при
    # кроці 3,17 с, значить крок замалий і медіана з трьох нічого не прибирає.
    soil_min: int | None = Field(default=None)
    soil_max: int | None = Field(default=None)
    # Скільки відліків усереднено. Константа збірки, але їде в даних навмисно:
    # інакше зміна SOIL_SAMPLES мовчки змінить сенс soil_raw заднім числом.
    soil_n: int | None = Field(default=None)
    rssi: int | None = Field(default=None)
    snr: float | None = Field(default=None)
    # Живлення вузла. Nullable, бо старі записи його не мають, а прошивка може
    # не слати (наприклад, вузол на USB без батареї).
    #
    # Потрібне для етапу 3: опорний вузол і вузол зі сном розряджають однакові
    # комірки поруч, і порівняти криві розряду можна лише якщо обидва значення
    # доїжджають до БД. Раніше `vbat` приїздив у пакеті й мовчки губився —
    # pydantic ігнорує зайві ключі.
    vbat: float | None = Field(default=None)
    # Секунди від старту вузла. Для опорного вузла це детектор тихого ресету:
    # неперервність — його головна властивість, і падіння лічильника до нуля
    # має бути видно в даних, а не здогадкою.
    uptime: int | None = Field(default=None)
    # Лічильник пробуджень із RTC-пам'яті; шле лише `-lowpower`. Переживає
    # deep sleep, але не повне зняття живлення, тому падіння до нуля означає
    # саме втрату живлення, а не звичайний цикл.
    boot: int | None = Field(default=None)
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
    # Усі — None, коли в бакеті жоден запис не має цього сенсора (напр. вузол
    # прислав пакет без температури). Раніше такий бакет валив увесь запит
    # ValidationError'ом, бо SensorStats вимагав float.
    air_temperature: SensorStats | None = None
    air_humidity: SensorStats | None = None
    soil_moisture: SensorStats | None = None
    soil_raw: SensorStats | None = None

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

    def get_latest(self, node_id: str) -> Measurement | None:
        with Session(self.engine) as session:
            stmt = (
                select(Measurement)
                .where(Measurement.node_id == node_id)
                .order_by(Measurement.timestamp.desc())
                .limit(1)
            )
            return session.exec(stmt).first()

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
                    # Сире ADC поруч із відсотком: етап 3 порівнює самі сенсори,
                    # а формула soilRawToPercent() ще не калібрована, тож для
                    # порівняння вузлів raw чесніший. avg/min/max ігнорують NULL
                    # (старі записи й USB-вузли без raw), avg=None якщо всі NULL.
                    func.min(Measurement.soil_raw).label("r_min"),
                    func.max(Measurement.soil_raw).label("r_max"),
                    func.avg(Measurement.soil_raw).label("r_avg"),
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


@router.get("/latest")
def read_latest_measurement(node_id: str, repo: RepositoryDep) -> Measurement | None:
    return repo.get_latest(node_id)


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

    def stats(row: dict, lo: str, hi: str, av: str) -> SensorStats | None:
        # avg NULL означає, що в бакеті не було жодного не-NULL значення сенсора.
        return None if row[av] is None else SensorStats(
            min=row[lo], max=row[hi], avg=row[av]
        )

    return [
        AggregateBucket(
            bucket=row["bucket"],
            count=row["count"],
            air_temperature=stats(row, "t_min", "t_max", "t_avg"),
            air_humidity=stats(row, "h_min", "h_max", "h_avg"),
            soil_moisture=stats(row, "m_min", "m_max", "m_avg"),
            soil_raw=stats(row, "r_min", "r_max", "r_avg"),
        )
        for row in rows
    ]
