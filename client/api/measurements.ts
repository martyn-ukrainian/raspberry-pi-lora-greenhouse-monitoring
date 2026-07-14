import { apiClient } from "./client";
import type { Measurement, AggregateBucket } from "../types";

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

export async function getAggregate(
  nodeId: string,
  bucketMinutes: number = 5,
): Promise<AggregateBucket[]> {
  const params = new URLSearchParams({
    node_id: nodeId,
    bucket_minutes: String(bucketMinutes),
  });

  return apiClient<AggregateBucket[]>(`/measurements/aggregate?${params}`)
}
