import "./global.css";

import { NavigationContainer, DefaultTheme } from "@react-navigation/native";
import { createBottomTabNavigator } from "@react-navigation/bottom-tabs";
import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import { GestureHandlerRootView } from "react-native-gesture-handler";
import { Ionicons } from "@expo/vector-icons";

import HomeScreen from "./screens/HomeScreen";
import AlertsScreen from "./screens/AlertsScreen";
import SettingsScreen from "./screens/SettingsScreen";


import { SafeAreaProvider } from "react-native-safe-area-context";

const Tab = createBottomTabNavigator();

const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      staleTime: 30_000,
      retry: 2,
      refetchOnWindowFocus: true,
    },
  },
});

const AgroTheme = {
    ...DefaultTheme,
    colors: {
      ...DefaultTheme.colors,
      primary: "#586E5A",        // brand — активний таб, кнопки
      background: "#ffffff",      // фон навколо screens
      card: "#ffffff",            // header + tab bar
      text: "#1c1917",            // stone-900 — основний текст
      border: "#e7e5e4",          // stone-200 — лінії
      notification: "#ef4444",   // алерти
    }
  };

export default function App() {
  const screens = [
    {
      name: "Home",
      component: HomeScreen,
      title: "Головна",
      icon: "home",
    },
    {
      name: "Alerts",
      component: AlertsScreen,
      title: "Алерти",
      icon: "notifications",
    },
    {
      name: "Settings",
      component: SettingsScreen,
      title: "Налаштування",
      icon: "settings",
    },
  ] as const

  return (
    <GestureHandlerRootView style={{ flex: 1 }}>
      <QueryClientProvider client={queryClient}>
        <SafeAreaProvider>
          <NavigationContainer theme={AgroTheme} >
            <Tab.Navigator screenOptions={{
              headerShown: false,
              tabBarStyle: {
                height: 80,
                paddingTop: 4,
                borderTopColor: "#e7e5e4",
                backgroundColor: "#ffffff"//"#f9f9f1"
              },
            }}>
              {screens.map((item) => (
                <Tab.Screen
                  key={item.name}
                  name={item.name}
                  component={item.component}
                  options={{
                    title: item.title,
                    tabBarIcon: ({ color, size }) => (
                      <Ionicons name={item.icon} color={color} size={size} />
                    ),
                    ...(item.name === "Alerts" && {
                      tabBarBadge: item.name === "Alerts" ? 3 : undefined,
                      tabBarBadgeStyle: { backgroundColor: "#ef4444" },
                    })
                  }}
                ></Tab.Screen>
              ))}
            </Tab.Navigator>
          </NavigationContainer>
        </SafeAreaProvider>
      </QueryClientProvider>
    </GestureHandlerRootView>
  );
}
