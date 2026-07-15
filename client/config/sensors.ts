import type { Ionicons } from "@expo/vector-icons";

export type SensorKey = "air_temperature" | "air_humidity" | "soil_moisture";

type IconName = keyof typeof Ionicons.glyphMap

export type SensorMeta = {
  key: SensorKey;
  shortLabel: string;
  fullLabel: string;
  briefLabel: string;
  unit: string;
  color: string;
  icon: IconName;
}

export const SENSORS: Record<SensorKey, SensorMeta> = {
  air_temperature: {
    key: "air_temperature",
    shortLabel: "Повітря",
    fullLabel: "Температура повітря",
    briefLabel: "Температура",
    unit: "°",
    color: "#586E5A",
    icon: "thermometer",
  },
  air_humidity: {
    key: "air_humidity",
    shortLabel: "Волога",
    fullLabel: "Вологість повітря",
    briefLabel: "Вологість",
    unit: "%",
    color: "#3b82f6",
    icon: "water",
  },
  soil_moisture: {
    key: "soil_moisture",
    shortLabel: "Ґрунт",
    fullLabel: "Вологість ґрунту",
    briefLabel: "Ґрунт",
    unit: "%",
    color: "#a16207",
    icon: "leaf",
  },
}

export const SENSOR_KEYS = Object.keys(SENSORS) as SensorKey[];
