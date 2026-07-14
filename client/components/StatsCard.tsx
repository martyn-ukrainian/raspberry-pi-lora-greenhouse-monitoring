import { View, Text } from "react-native";

type Props = {
  title: string;
  min: number;
  max: number;
  avg: number;
  unit?: string;
  inRangePct?: number;
}

export default function StatsCard({
  title,
  min,
  max,
  avg,
  unit = "",
  inRangePct,
}: Props) {
  return (
    <View className="bg-white p-4 rounded-lg mb-3">
      <Text className="text-brand text-base font-semibold mb-3">
        {title}
      </Text>

      <View className="flex-row justify-between">
        <View className="items-center flex-1">
          <Text className="text-stone-500 text-xs mb-1">Мін</Text>
          <Text className="text-lg font-semibold text-blue-600">
            {min.toFixed(1)}{unit}
          </Text>
        </View>
        <View className="items-center flex-1">
          <Text className="text-stone-500 text-xs mb-1">Сер</Text>
          <Text className="text-lg font-semibold text-brand">
            {avg.toFixed(1)}{unit}
          </Text>
        </View>
        <View className="items-center flex-1">
          <Text className="text-stone-500 text-xs mb-1">Макс</Text>
          <Text className="text-lg font-semibold text-red-600">
            {max.toFixed(1)}{unit}
          </Text>
        </View>
      </View>

      {inRangePct != null && (
        <View className="mt-4 pt-3 border-t border-stone-100">
          <View className="flex-row justify-between mb-1">
            <Text className="text-stone-500 text-xs">У межах норми</Text>
            <Text className="text-xs font-semibold">
              {inRangePct.toFixed(0)}%
            </Text>
          </View>
          <View className="h-2 bg-stone-100 rounded-full overflow-hidden">
            <View
              className="h-full bg-brand rounded-full"
              style={{ width: `${inRangePct}%` }}
            />
          </View>
        </View>
      )}
    </View>
  )
}
