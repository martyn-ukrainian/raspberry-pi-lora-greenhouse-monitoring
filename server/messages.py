from pathlib import Path

import yaml

_CONFIG_PATH = Path(__file__).parent / "config" / "messages.yaml"

with _CONFIG_PATH.open(encoding="utf-8") as f:
    _data = yaml.safe_load(f)

ICONS: dict = _data["icons"]
SENSOR_NAMES: dict[str, str] = _data["sensor_names"]
DIRECTIONS: dict[str, str] = _data["directions"]
UNITS: dict[str, str] = _data["units"]
