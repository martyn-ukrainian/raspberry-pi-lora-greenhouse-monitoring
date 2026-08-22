const API_BASE = import.meta.env.VITE_API_URL;

if (!API_BASE) {
  throw new Error(
    "VITE_API_URL не задано. Скопіюй web/.env.example у web/.env.local і вкажи адресу сервера.",
  );
}

async function apiClient<T>(path: string): Promise<T> {
  const response = await fetch(`${API_BASE}${path}`);
  if (!response.ok) {
    throw new Error(`HTTP ${response.status}: ${response.statusText}`);
  }
  return response.json();
}

export type Greenhouse = {
  node_id: string;
  label: string;
};

export type SensorStats = {
  min: number;
  max: number;
  avg: number;
};

export type AggregateBucket = {
  bucket: string;
  count: number;
  air_temperature: SensorStats;
  air_humidity: SensorStats;
  soil_moisture: SensorStats;
};

export function getGreenhouses(): Promise<Greenhouse[]> {
  return apiClient<Greenhouse[]>("/greenhouses");
}

export function getAggregate(nodeId: string): Promise<AggregateBucket[]> {
  const params = new URLSearchParams({ node_id: nodeId, bucket_minutes: "5" });
  return apiClient<AggregateBucket[]>(`/measurements/aggregate?${params}`);
}
