import { View, Text } from "react-native";
import { Ionicons } from "@expo/vector-icons";
import { useEffect, useRef } from "react";

import Screen from "../components/Screen";
import { useAlerts } from "../hooks/useAlerts";
import { useAckAllAlerts } from "../hooks/useAckAlerts";
import AlertCard from "../components/AlertCard";


export default function AlertsScreen() {
  const { data, isLoading, error, refetch, isRefetching } = useAlerts();
  const ackMutation = useAckAllAlerts();
  const ackedThisMount = useRef(false);

  useEffect(() => {
    if (!data || ackedThisMount.current) return;

    const hasUnread = data.some((a) => !a.acknowledged);
    if (hasUnread) {
      ackedThisMount.current = true;
      ackMutation.mutate();
    }
  }, [data]);

  return (
    <Screen
      hasLoading={isLoading}
      error={error as Error | null}
      onRetry={refetch}
      refreshing={isRefetching}
      onRefresh={refetch}
    >
      <Text className="text-brand text-2xl font-bold my-2">Алерти</Text>

      {data && data.length === 0 && (
        <View className="items-center py-8">
          <Ionicons name="checkmark-circle" size={48} color="#586E5A" />
          <Text className="text-stone-500 mt-2">Все спокійно, алертів немає.</Text>
        </View>
      )}

      {data?.map((a) => (<AlertCard key={a.id} alert={a}/>))}
    </Screen>
  )
}
