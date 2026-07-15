import { apiClient } from "./client";
import type { Alert } from "../types";

export async function getAlerts(limit: number = 50): Promise<Alert[]> {
  return apiClient<Alert[]>("/alerts");
}


export async function ackAllAlerts(): Promise<{ acknowledged: number }> {
  return apiClient("/alerts/ack-all", { method: "POST" });
}
