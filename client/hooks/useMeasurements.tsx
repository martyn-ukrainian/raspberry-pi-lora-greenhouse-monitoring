import { useQuery } from "@tanstack/react-query";
import { getMeasurements } from "../api/measurements";

export function useMeasurements() {
  return useQuery({
    queryKey: ["measurements"],
    queryFn: getMeasurements,
    refetchInterval: 15_000,            // авто-refetch кожні 30 сек
    refetchIntervalInBackground: false, // не refetch коли app у бекграунді
  })
}
