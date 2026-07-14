import { useState } from "react"
import { View, Text } from "react-native"

import Screen from "../components/Screen";
import GreenhouseCard from "../components/GreenhouseCard";
import { useGreenhouses } from "../hooks/useGreenhouses";


export default function HomeScreen() {
  const [bucketMinutes] = useState(5);   // потім setBucketMinutes для селектора
  const { data: greenhouses, isLoading, error, refetch } = useGreenhouses();

  return (
    <Screen
      hasLoading={isLoading}
      error={error as Error | null}
      onRetry={refetch}
    >
      <View className="mb-2">
        <Text className="text-brand font-bold my-2 text-2xl">Теплиці</Text>
      </View>
      {greenhouses?.map((g) => (
        <GreenhouseCard
          key={g.node_id}
          nodeId={g.node_id}
          bucketMinutes={bucketMinutes}
        />
      ))}
    </Screen>
  )
}
