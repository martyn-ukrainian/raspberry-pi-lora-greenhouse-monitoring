import { View, Text } from "react-native";
import { MaterialCommunityIcons } from "@expo/vector-icons";

type Props = {
  rssi: number | null;
  snr: number | null;
};

type IconName = keyof typeof MaterialCommunityIcons.glyphMap;

// Пороги орієнтовні (LoRa 868 МГц, SX1262) — RSSI ближче до 0 і SNR вище
// нуля означають запас потужності; від'ємний SNR — це нормально для LoRa,
// критичний поріг лежить набагато нижче (десь біля -15..-20 дБ).
function classify(rssi: number, snr: number): { color: string; label: string; icon: IconName } {
  if (rssi > -90 && snr > 0) {
    return { color: "#16a34a", label: "Сильний сигнал", icon: "wifi-strength-4" };
  }
  if (rssi > -110 && snr > -10) {
    return { color: "#ca8a04", label: "Середній сигнал", icon: "wifi-strength-2" };
  }
  return { color: "#dc2626", label: "Слабкий сигнал", icon: "wifi-strength-1" };
}

export default function SignalIndicator({ rssi, snr }: Props) {
  if (rssi == null || snr == null) return null;

  const { color, label, icon } = classify(rssi, snr);

  return (
    <View
      className="flex-row items-center"
      accessibilityLabel={`${label}: ${rssi} дБм, SNR ${snr} дБ`}
    >
      <MaterialCommunityIcons name={icon} size={16} color={color} />
      <Text className="ml-1 text-stone-500 text-xs">{rssi}дБм</Text>
    </View>
  );
}
