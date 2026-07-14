from typing import Annotated

from fastapi import APIRouter, Depends
from pydantic import BaseModel

from logger import get_logger
from thresholds import thresholds

logger = get_logger(__name__)


class SensorRange(BaseModel):
    min: float | None = None
    max: float | None = None


class GreenhouseThresholds(BaseModel):
    air_temperature: SensorRange
    air_humidity: SensorRange
    soil_moisture: SensorRange


class Greenhouse(BaseModel):
    node_id: str
    label: str
    thresholds: GreenhouseThresholds


class GreenhousesRepository:
    def list_all(self) -> list[Greenhouse]:
        return [
            Greenhouse(
                node_id=node_id,
                label=cfg.label,
                thresholds=GreenhouseThresholds(
                    air_temperature=SensorRange(
                        min=cfg.air_temperature.min, max=cfg.air_temperature.max
                    ),
                    air_humidity=SensorRange(
                        min=cfg.air_humidity.min, max=cfg.air_humidity.max
                    ),
                    soil_moisture=SensorRange(
                        min=cfg.soil_moisture.min, max=cfg.soil_moisture.max
                    ),
                ),
            )
            for node_id, cfg in thresholds.greenhouses.items()
        ]


def get_repository() -> GreenhousesRepository:
    return GreenhousesRepository()


router = APIRouter(prefix="/greenhouses", tags=["greenhouses"])

RepositoryDep = Annotated[GreenhousesRepository, Depends(get_repository)]


@router.get("")
def read_greenhouses(repo: RepositoryDep) -> list[Greenhouse]:
    greenhouses = repo.list_all()
    logger.info("Returned %d greenhouses", len(greenhouses))
    return greenhouses
