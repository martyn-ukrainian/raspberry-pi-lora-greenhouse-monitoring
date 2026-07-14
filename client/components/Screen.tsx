import { ReactNode } from "react";
import { ActivityIndicator, Pressable, RefreshControl, ScrollView, Text, View } from "react-native";
import { SafeAreaView } from "react-native-safe-area-context";

type ScreenProps = {
  children: ReactNode;
  title?: string;
  hasLoading?: boolean;
  error?: Error | null;
  onRetry?: () => void;
  refreshing?: boolean;
  onRefresh?: () => void;

}

export default function Screen({
  children,
  title,
  hasLoading,
  error,
  onRetry,
  refreshing = false,
  onRefresh
}: ScreenProps) {
  return (
    <SafeAreaView edges={["top"]} className="flex-1 bg-stone-100">
      {hasLoading ? (
        <View className="flex-1 items-center justify-center pt-16">
          <ActivityIndicator size="large" color="#586E5A" />
        </View>
      ) : error ? (
        <View className="flex-1 items-center justify-center px-4">
          <View className="w-full bg-red-100 p-4 rounded-md">
            <Text className="text-red-800 font-semibold mb-2">
              Не вдалось завантажити
            </Text>
            <Text className="text-red-700 mb-3">{error.message}</Text>
            {onRetry && (
              <Pressable
                onPress={onRetry}
                className="bg-red-500 border-red-500 border py-3 px-4 mx-auto mt-6 rounded-md self-start"
              >
                <Text className="text-red-100 font-semibold">Спробувати знову</Text>
              </Pressable>
            )}
          </View>
        </View>
        ) : (
          <ScrollView
            refreshControl={onRefresh && (<RefreshControl refreshing={!!refreshing} onRefresh={onRefresh} />)}
            className="flex-1 p-2">
            {title && (
              <Text className="text-brand text-2xl font-bold text-center mb-4">
                {title}
              </Text>
            )}
            {children}
          </ScrollView>
      )}
    </SafeAreaView>
  )
}
