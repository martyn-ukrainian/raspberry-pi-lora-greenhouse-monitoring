import { useQuery } from "@tanstack/react-query";
import { getAggregate } from "../api/measurements";

type Options = {
  bucketMinutes?: number;
}

export function useAggregate(nodeId: string, options: Options = {}) {
  const bucketMinutes = options.bucketMinutes ?? 5;

  return useQuery({
    queryKey: ["aggregate", nodeId, bucketMinutes],
    queryFn: () => getAggregate(nodeId, bucketMinutes),
    refetchInterval: 30_000,
  });
}
