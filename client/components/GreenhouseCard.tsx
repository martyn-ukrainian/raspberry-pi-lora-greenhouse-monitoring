import { View, Text, Pressable } from "react-native";
import { useNavigation } from "@react-navigation/native";
import { Ionicons } from "@expo/vector-icons";
import type { NativeStackNavigationProp } from "@react-navigation/native-stack";

import MiniChart from "./MiniChart";
import { useAggregate } from "../hooks/useAggregate";
import { useGreenhouses } from "../hooks/useGreenhouses";
import { formatTime } from "../utils/dateformat";
import type { HomeStackParamList } from "../types/navigation";
import { SENSORS, SENSOR_KEYS } from "../config/sensors";

type Props = {
  nodeId: string;
  bucketMinutes: number;
}

type NavProp = NativeStackNavigationProp<HomeStackParamList, "HomeList">;

export default function GreenhouseCard({ nodeId, bucketMinutes }: Props) {
  const navigation = useNavigation<NavProp>();
  const { data: buckets } = useAggregate(nodeId, { bucketMinutes });
  const { data: greenhouses } = useGreenhouses();

  const config = greenhouses?.find(g => g.node_id === nodeId);
  const label = config?.label ?? nodeId;

  const chartData = (buckets ?? []).map(b => ({
    x: new Date(b.bucket).getTime(),
    y: b.air_temperature.avg,
  }));

  const latest = buckets?.[buckets.length - 1];

  return (
    <Pressable
      onPress={() => navigation.navigate("GreenhouseDetail", { nodeId })}
      className="bg-white p-4 rounded-lg mb-3 active:opacity-80"
    >
      <View className="flex-row justify-between items-start">
        <Text className="text-stone-800 text-lg font-semibold mb-2">
          {label}
        </Text>
        {latest && (
          <Text className="text-stone-500">{formatTime(latest.bucket)}</Text>
        )}
      </View>

      {latest && (
        <View className="flex-row mb-3 gap-4">
          {SENSOR_KEYS.map((key) => {
            const meta = SENSORS[key];
            const value = latest[key].avg;
            const isPercent = meta.unit === "%";
            return (
              <View key={key} className="flex-row items-center">
                <Ionicons name={meta.icon} size={16} color={meta.color} />
                <Text className="ml-1 text-stone-900 font-medium">
                  {value.toFixed(isPercent ? 0 : 1)}{meta.unit}
                </Text>
              </View>
            )
          })}
        </View>
      )}

      <MiniChart
        data={chartData}
        min={config?.thresholds.air_temperature.min}
        max={config?.thresholds.air_temperature.max}
      />

    </Pressable>
  )
}
