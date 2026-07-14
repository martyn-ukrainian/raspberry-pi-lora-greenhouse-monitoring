import { View, Text } from "react-native";
import MiniChart from "./MiniChart";
import { useAggregate } from "../hooks/useAggregate";
import { useGreenhouses } from "../hooks/useGreenhouses";
import { formatTime } from "../utils/dateformat";

type Props = {
  nodeId: string;
  bucketMinutes: number;
}

export default function GreenhouseCard({ nodeId, bucketMinutes }: Props) {
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
    <View className="bg-white p-4 rounded-lg mb-3">
      <View className="flex-row justify-between items-start">
        <Text className="text-brand text-lg font-semibold mb-2">
          {label}
        </Text>
        {latest && (
          <Text className="text-stone-500">{formatTime(latest.bucket)}</Text>
        )}
      </View>

      <MiniChart
        data={chartData}
        min={config?.thresholds.air_temperature.min}
        max={config?.thresholds.air_temperature.max}
      />

      {latest && (
        <View className="flex-row mt-3">
          <Text className="text-stone-900 mr-4">
            T: {latest.air_temperature.avg.toFixed(1)}°C
          </Text>
          <Text className="text-stone-900 mr-4">
            H: {latest.air_humidity.avg.toFixed(0)}%
          </Text>
          <Text className="text-stone-900">
            M: {latest.soil_moisture.avg.toFixed(0)}%
          </Text>
        </View>
      )}
    </View>
  )
}
