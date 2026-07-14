import { useLayoutEffect } from "react";
import { View, Text } from "react-native";
import type { NativeStackScreenProps } from "@react-navigation/native-stack";

import Screen from "../components/Screen";
import { useGreenhouses } from "../hooks/useGreenhouses";
import type { HomeStackParamList } from "../types/navigation";

type Props = NativeStackScreenProps<HomeStackParamList, "GreenhouseDetail">;

export default function GreenhouseDetailScreen({ route, navigation }: Props) {
  const { nodeId } = route.params;
  const { data: greenhouses } = useGreenhouses();
  const config = greenhouses?.find(g => g.node_id === nodeId);

  useLayoutEffect(() => {
    if (config?.label) {
      navigation.setOptions({ title: config.label });
    }
  }, [navigation, config?.label]);

  return (
    <Screen>
      <Text className="text-brand text-2xl font-bold mb-4">
        {config?.label ?? nodeId}
      </Text>

      <View className="bg-white p-4 rounded-lg mb-3">
        <Text className="text-stone-500">
          Тут буде chart
        </Text>
      </View>
    </Screen>
  )
}
