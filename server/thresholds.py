"""
Завантаження та валідація конфігурації порогів з config/thresholds.yaml.

Файл читається один раз при імпорті. Якщо YAML невалідний (неправильні типи,
пропущене поле, максимум < мінімум) — сервер не стартоне і падає з зрозумілою
помилкою. Це саме те що треба: краще впасти на старті ніж пропускати алерти
через тиху помилку конфігу.

Опис формату — у docs/alerts.md.
"""

from pathlib import Path

import yaml
from pydantic import BaseModel, model_validator

from logger import get_logger

logger = get_logger(__name__)


class SensorThresholds(BaseModel):
    min: float | None = None
    max: float | None = None
    dwell_minutes: int | None = None

    @model_validator(mode="after")
    def check_min_lt_max(self) -> "SensorThresholds":
        if self.min is not None and self.max is not None and self.min > self.max:
            raise ValueError(f"min ({self.min}) must be <= max ({self.max})")
        return self


class GreenhouseConfig(BaseModel):
    """Пороги для однієї теплиці."""

    label: str
    air_temperature: SensorThresholds
    air_humidity: SensorThresholds
    soil_moisture: SensorThresholds


class Defaults(BaseModel):
    """Глобальні дефолти для витримки й cooldown, коли в теплиці не задано."""

    dwell_minutes: int = 5
    cooldown_minutes: int = 30


class ThresholdsConfig(BaseModel):
    """Кореневий обʼєкт всієї конфігурації порогів."""

    defaults: Defaults
    greenhouses: dict[str, GreenhouseConfig]


CONFIG_PATH = Path(__file__).parent / "config" / "thresholds.yaml"


def load_thresholds() -> ThresholdsConfig:
    logger.info("Loading thresholds config from %s", CONFIG_PATH)
    with CONFIG_PATH.open(encoding="utf-8") as f:
        raw = yaml.safe_load(f)
    config = ThresholdsConfig(**raw)

    logger.info(
        "Thresholds loaded: %d greenhouse(s), defaults dwell=%dm cooldown=%dm",
        len(config.greenhouses),
        config.defaults.dwell_minutes,
        config.defaults.cooldown_minutes,
    )

    return config


thresholds = load_thresholds()
