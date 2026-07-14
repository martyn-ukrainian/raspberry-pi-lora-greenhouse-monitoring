export type Measurement = {
  id: number;
  node_id: string;
  air_temperature: number;
  air_humidity: number;
  soil_moisture: number;
  timestamp: string;
}

export type SensorStats = {
  min: number;
  max: number;
  avg: number;
}

export type AggregateBucket = {
  bucket: string;
  count: number;
  air_temperature: SensorStats;
  air_humidity: SensorStats;
  soil_moisture: SensorStats;
}


type SensorRange = {
  min: number | null;
  max: number | null;
}

type GreenhouseThresholds = {
  air_temperature: SensorRange;
  air_humidity: SensorRange;
  soil_moisture: SensorRange;
}

export type Greenhouse = {
  node_id: string;
  label: string;
  thresholds: GreenhouseThresholds;
}


export type Alert = {

}
