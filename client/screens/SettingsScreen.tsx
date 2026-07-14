import { View, Text } from "react-native";
import Screen from "../components/Screen";
import { useGreenhouses } from "../hooks/useGreenhouses";

export default function SettingsScreen() {
  const { data } = useGreenhouses();
  return (
    <Screen>
      <Text className="text-brand text-2xl font-bold mb-2">Settings</Text>
      <View >
        {data?.map((g) => (
          <View className="bg-white p-4 rounded-lg mb-3">
            <Text className="text-xl font-bold mb-3">{g.label}</Text>
            <View className="flex-row items-center">
              <Text className="text-lg font-semibold mr-2">Температура:</Text>
              <Text className="text-lg">Мін: {g.thresholds?.air_temperature.min}, Макс: {g.thresholds?.air_temperature.max}</Text>
            </View>
            <View className="flex-row items-center">
              <Text className="text-lg font-semibold mr-2">Волога:</Text>
              <Text className="text-lg">Мін: {g.thresholds?.air_humidity.min}%, Макс: {g.thresholds?.air_humidity.max}%</Text>
            </View>

            <View className="flex-row items-center">
              <Text className="text-lg font-semibold mr-2">Земля:</Text>
              <Text className="text-lg">Мін: {g.thresholds?.soil_moisture.min}%, Макс: {g.thresholds?.soil_moisture.max}%</Text>
            </View>
            {/*{g.thresholds?.map((item) => <Text>{item?.toString()}</Text>)}*/}
          </View>
        ))}
      </View>
    </Screen>
  )
}
