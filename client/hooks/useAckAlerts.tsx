import { useMutation, useQueryClient } from "@tanstack/react-query";
import { ackAllAlerts } from "../api/alerts";
import type { Alert } from "../types";

export function useAckAllAlerts() {
  const client = useQueryClient();
  return useMutation({
    mutationFn: ackAllAlerts,

    onMutate: async () => {
      // 1. Cancel pending queries
      await client.cancelQueries({ queryKey: ["alerts"] });

      const previous = client.getQueriesData<Alert[]>({ queryKey: ["alerts"] });

      client.setQueriesData<Alert[]>({ queryKey: ["alerts"] }, (old) =>
        old?.map((a) => ({ ...a, acknowledged: true })),
      );

      return { previous }
    },

    onError: (_err, _vars, ctx) => {
      if (ctx?.previous) {
        for (const [key, data] of ctx.previous) {
          client.setQueryData(key, data);
        }
      }
    },

    onSettled: () => {
      client.invalidateQueries({ queryKey: ["alerts"] });
    },

    // onSuccess: () => {
    //   // інвалідуємо кеш алертів — TanStack перечитає з бекенду
    //   client.invalidateQueries({ queryKey: ["alerts"] });
    // }
  })
}
