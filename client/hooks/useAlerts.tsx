import { useQuery } from "@tanstack/react-query";
import { getAlerts } from "../api/alerts";

export function useAlerts(limit: number = 50) {
  return useQuery({
    queryKey: ["alerts", limit],
    queryFn: () => getAlerts(limit),
    refetchInterval: 60_000,
  })
}
