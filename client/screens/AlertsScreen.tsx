import { Text } from "react-native";
import Screen from "../components/Screen";


export default function AlertsScreen() {
  return (
    <Screen title="Alerts">
      <Text className="text-brand text-2xl font-bold mb-2">Алерти</Text>
      <Text className="text-stone-900 text-center">
        Історія повідомлень зʼявиться тут.
      </Text>
    </Screen>
  );
}
