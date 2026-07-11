"""
Тест для alerts.py
Починаємо з чистих функцій без стану - легко покривати, легко читати.
"""

from datetime import UTC, datetime, timedelta

import pytest

from alerts import _check_sensor, _classify, _state
from thresholds import SensorThresholds


@pytest.fixture(autouse=True)
def clear_state():
    _state.clear()
    yield
    _state.clear()


def test_classify_value_in_range_returns_none() -> None:
    config = SensorThresholds(min=20.0, max=30.0)

    kind, boundary = _classify(25.0, config)

    assert kind is None
    assert boundary is None


def test_classify_value_above_max_returns_high() -> None:
    config = SensorThresholds(min=20.0, max=30.0)

    kind, boundary = _classify(35.0, config)

    assert kind == "high"
    assert boundary == 30.0


def test_classify_value_above_min_returns_low() -> None:
    config = SensorThresholds(min=20.0, max=30.0)

    kind, boundary = _classify(15.0, config)

    assert kind == "low"
    assert boundary == 20.0


def test_check_sensor_in_range_returns_none() -> None:
    config = SensorThresholds(min=20.0, max=30.0, dwell_minutes=5)
    now = datetime(2026, 7, 11, 12, 0, tzinfo=UTC)

    result = _check_sensor(
        node_id="test-1",
        label="Test",
        sensor_name="air_temperature",
        value=25.0,
        sensor_config=config,
        timestamp=now,
    )

    assert result is None


def test_check_sensor_dwell_passed_returns_alert() -> None:
    config = SensorThresholds(min=20.0, max=30.0, dwell_minutes=5)
    start = datetime(2026, 7, 11, 12, 0, tzinfo=UTC)

    # Перший вимір: поза межами, фіксується out_since
    _check_sensor(
        node_id="test-1",
        label="Test",
        sensor_name="air_temperature",
        value=35.0,
        sensor_config=config,
        timestamp=start,
    )

    # Через 6 хв (dwell 5 хв пройшло)
    later = start + timedelta(minutes=6)
    result = _check_sensor(
        node_id="test-1",
        label="Test",
        sensor_name="air_temperature",
        value=35.0,
        sensor_config=config,
        timestamp=later,
    )

    assert result is not None
    assert result.kind == "high"
    assert result.value == 35.0
    assert result.boundary == 30.0
    assert result.duration_minutes == 6


def test_check_sensor_cooldown_blocks_repeat_alert() -> None:
    config = SensorThresholds(min=20.0, max=30.0, dwell_minutes=5)
    start = datetime(2026, 7, 11, 12, 0, tzinfo=UTC)

    # Прогріваємо state до першого алерта
    _check_sensor(
        node_id="test-1",
        label="Test",
        sensor_name="air_temperature",
        value=35.0,
        sensor_config=config,
        timestamp=start,
    )
    first_alert = _check_sensor(
        node_id="test-1",
        label="Test",
        sensor_name="air_temperature",
        value=35.0,
        sensor_config=config,
        timestamp=start + timedelta(minutes=6),
    )
    assert first_alert is not None  # sanity check

    # Ще через 10 хв (cooldown 30 хв не пройшов)
    result = _check_sensor(
        node_id="test-1",
        label="Test",
        sensor_name="air_temperature",
        value=35.0,
        sensor_config=config,
        timestamp=start + timedelta(minutes=16),
    )

    assert result is None
