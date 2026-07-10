from fastapi import APIRouter, Depends
from sqlmodel import Field, Session, SQLModel, select

from database import engine


class Measurement(SQLModel, table=True):
    id: int | None = Field(default=None, primary_key=True)
    node_id: str
    air_temperature: float
    air_humidity: float
    soil_moisture: float


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


@router.post("")
def create_measurement(
    measurement: Measurement, repo: MeasurementRepository = Depends(get_repository)
) -> Measurement:
    return repo.create(measurement)


@router.get("")
def read_measurement(
    repo: MeasurementRepository = Depends(get_repository),
) -> list[Measurement]:
    return repo.list_all()
