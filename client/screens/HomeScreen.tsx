import { useState } from "react"
import { View, Text } from "react-native"

import Screen from "../components/Screen";
import MiniChart from "../components/MiniChart";

import { useMeasurements } from "../hooks/useMeasurements";
import { useGreenhouses } from "../hooks/useGreenhouses";
import { formatTime } from "../utils/dateformat";


import type { Measurement } from "../types"

export default function HomeScreen() {
  const { data, isLoading, error, refetch } = useMeasurements()
  const { data: gData } = useGreenhouses()
  const [refreshing, setRefreshing] = useState(false)

  const handleRefresh = async () => {
    setRefreshing(true)
    try {
      await refetch()
    } finally {
      setRefreshing(false)
    }
  }

  const measurements = data ?? [];

  const historyByNode = measurements.reduce<Record<string, Measurement[]>>(
    (acc, m) => {
      (acc[m.node_id] ??= []).push(m);
      return acc;
    },
    {}
  );

  const greenhouses = Object.entries(historyByNode)
    .map(([nodeId, list]) => {
      const sorted = [...list].sort((a, b) => a.timestamp.localeCompare(b.timestamp));

      return {
        nodeId,
        latest: sorted[sorted.length - 1],
        history: sorted.slice(-20),
      }
    });

  const labels = Object.fromEntries(
    (gData ?? []).map(g => [g.node_id, g.label])
  )

  const configByNode = Object.fromEntries(
    (gData ?? []).map(g => [g.node_id, g])
  );

  // const latestByNode = measurements.reduce<Record<string, typeof measurements[0]>>(
  //   (acc, m) => {
  //     if (!acc[m.node_id] || m.timestamp > acc[m.node_id].timestamp) {
  //       acc[m.node_id] = m;
  //     }

  //     return acc;
  //   },
  //   {},
  // );

  // const greenhouses = Object.values(latestByNode);


  return (
    <Screen
      hasLoading={isLoading}
      error={error as Error | null}
      onRetry={refetch}
      refreshing={refreshing}
      onRefresh={handleRefresh}
    >
      <View className="mb-2">
        <Text className="text-brand font-bold my-2 text-2xl">Теплиці</Text>
      </View>
      {greenhouses.map((g) => {
        const chartData = g.history.map((m, i) => ({ x: i, y: m.air_temperature }))
        return (
          <View key={g.nodeId} className="bg-white p-4 rounded-lg mb-3">
            <View className="flex-row justify-between items-start">
              <Text className="text-brand text-lg font-semibold mb-2">
                {labels[g.nodeId] ?? g.nodeId}
              </Text>

              <Text className="text-stone-500">{formatTime(g.latest.timestamp)}</Text>
            </View>

            <MiniChart
              data={chartData}
              min={configByNode[g.nodeId]?.thresholds.air_temperature.min}
                max={configByNode[g.nodeId]?.thresholds.air_temperature.max}
            />
            <View className="flex-row mt-3">
              <Text className="text-stone-900 mr-4">T: {g.latest.air_temperature.toFixed(1)}°C</Text>
              <Text className="text-stone-900 mr-4">H: {g.latest.air_humidity.toFixed(0)}%</Text>
              <Text className="text-stone-900">M: {g.latest.soil_moisture.toFixed(0)}%</Text>
            </View>
          </View>
        )
      })}
    </Screen>
  )
}
