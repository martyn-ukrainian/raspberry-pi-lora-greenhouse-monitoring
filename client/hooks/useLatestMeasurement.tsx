import { useQuery } from "@tanstack/react-query";
import { getLatestMeasurement } from "../api/measurements";

export function useLatestMeasurement(nodeId: string) {
  return useQuery({
    queryKey: ["latestMeasurement", nodeId],
    queryFn: () => getLatestMeasurement(nodeId),
    refetchInterval: 30_000,
  });
}
