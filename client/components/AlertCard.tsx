import { View, Text } from "react-native";
import { Ionicons } from "@expo/vector-icons";

import { formatTime } from "../utils/dateformat";
import type { Alert } from "../types";
import { SENSORS } from "../config/sensors";

type Props = {
  alert: Alert;
  onPress?: () => void;
}

const KIND_ICON: Record<Alert["kind"], { name: keyof typeof Ionicons.glyphMap; color: string }> = {
  high: { name: "flame", color: "#ef4444" },
  low: { name: "snow", color: "#3b82f6" },
}

const KIND_LABEL: Record<Alert["kind"], string> = {
  high: "вище норми",
  low: "нижче норми",
}

export default function AlertCard({ alert }: Props) {
  const icon = KIND_ICON[alert.kind];

  return (
    <View className="bg-white p-4 rounded-lg mb-3 flex-row">
      <View className="mr-3 mt-0.5">
        <Ionicons name={icon.name} size={22} color={icon.color}/>
      </View>

      <View className="flex-1">
        <Text className="text-brand font-semibold">{alert.label}</Text>
        <Text className="text-stone-900 mt-0.5">
          <Ionicons
            name={SENSORS[alert.sensor].icon}
            size={14}
            color={SENSORS[alert.sensor].color}
          />
          {" "}
          {SENSORS[alert.sensor].briefLabel} {KIND_LABEL[alert.kind]}</Text>
        <Text className="text-stone-600 text-sm mt-0.5">
          {alert.value.toFixed(1)} · межа {alert.boundary.toFixed(1)} · {alert.duration_minutes} хв
        </Text>
        <Text className="text-stone-400 text-xs mt-1">{formatTime(alert.created_at)}</Text>
      </View>
    </View>
  )
}
