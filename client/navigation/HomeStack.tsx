import { createNativeStackNavigator } from "@react-navigation/native-stack";
import HomeScreen from "../screens/HomeScreen";
import GreenhouseDetailScreen from "../screens/GreenhouseDetailScreen";
import type { HomeStackParamList } from "../types/navigation";

const Stack = createNativeStackNavigator<HomeStackParamList>();

export default function HomeStack() {
  return (
    <Stack.Navigator
      screenOptions={{
        headerTintColor: "#586E5A",
        headerTitleStyle: { color: "#586E5A" },
      }}
    >
      <Stack.Screen
        name="HomeList"
        component={HomeScreen}
        options={{ headerShown: false }}
      />

      <Stack.Screen
        name="GreenhouseDetail"
        component={GreenhouseDetailScreen}
        options={{
          headerTintColor: "#586E5A",
          headerTitleStyle: { color: "#586E5A", fontSize: 17 },
          headerBackButtonDisplayMode: "minimal",
          // title: "Теплиця",
          // headerTintColor: "#586E5A",
          // headerTitleStyle: { color: "#586E5A" },
          // headerBackTitle: "Назад",              // ← текст біля стрілки
          // headerBackTitleStyle: { fontSize: 14 }, // ← менший розмір
          // headerBackButtonDisplayMode: "minimal",
        }}
      />
    </Stack.Navigator>
  )
}
