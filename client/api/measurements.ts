import { apiClient } from "./client";
import type { Measurement } from "../types";

export async function getMeasurements(): Promise<Measurement[]> {
  return apiClient<Measurement[]>("/measurements")
}

export async function createMeasurement(data: {
  node_id: string;
  air_temperature: number;
  air_humidity: number;
  soil_moisture: number;
}): Promise<Measurement> {
  return apiClient<Measurement>("/measurements", {
    method: "POST",
    body: JSON.stringify(data)
  })
}
