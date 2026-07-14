import { apiClient } from "./client";
import type { Greenhouse } from "../types";

export async function getGreenhouses(): Promise<Greenhouse[]> {
  return apiClient<Greenhouse[]>("/greenhouses")
}
