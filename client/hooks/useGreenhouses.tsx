import { useEffect } from "react";
import { useQuery, useQueryClient } from "@tanstack/react-query";

import { getGreenhouses } from "../api/greenhouses";

const queryOptions = {
    queryKey: ["greenhouses"],
    queryFn: getGreenhouses,
    staleTime: Infinity,
    gcTime: Infinity,
} as const

export function useGreenhouses() {
  return useQuery(queryOptions);
}

export function usePrefetchGreenhouses() {
  const client = useQueryClient();

  useEffect(() => {
    client.prefetchQuery(queryOptions);
  }, [client]);
}
