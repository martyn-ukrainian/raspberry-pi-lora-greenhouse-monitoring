import { useQuery } from "@tanstack/react-query";
import { getAggregate } from "../api/measurements";

type Options = {
  bucketMinutes?: number;
  since?: Date;
};

// Range → recommended bucket size, keeps chart at ~50-100 points.
// Wire up when the range selector lands.
//
//   Діапазон  | Bucket | Точок
//   ----------|--------|------
//   1 година  | 1 хв   | 60
//   4 години  | 5 хв   | 48
//   24 години | 15 хв  | 96
//   3 дні     | 1 год  | 72
//   Тиждень   | 3 год  | 56
//
//   function bucketForRange(hours: number): number {
//     if (hours <= 1) return 1;
//     if (hours <= 6) return 5;
//     if (hours <= 24) return 15;
//     if (hours <= 72) return 60;
//     return 180;
//   }

export function useAggregate(nodeId: string, options: Options = {}) {
  const bucketMinutes = options.bucketMinutes ?? 5;
  const since = options.since;

  return useQuery({
    queryKey: ["aggregate", nodeId, bucketMinutes, since?.toISOString()],
    queryFn: () => getAggregate(nodeId, bucketMinutes, since),
    refetchInterval: 30_000,
  });
}
