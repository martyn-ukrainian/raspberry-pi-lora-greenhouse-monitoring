import { useCallback, useLayoutEffect, useMemo, useState } from "react";
import { View, Text, Pressable } from "react-native";
import { Gesture, GestureDetector } from "react-native-gesture-handler";
import { runOnJS } from "react-native-reanimated";
import { Ionicons } from "@expo/vector-icons";
import type { NativeStackScreenProps } from "@react-navigation/native-stack";

import Screen from "../components/Screen";
import Chart from "../components/Chart";
import StatsCard from "../components/StatsCard";
import { useGreenhouses } from "../hooks/useGreenhouses";
import { useAggregate } from "../hooks/useAggregate";
import { summarizeBuckets } from "../utils/stats";
import type { HomeStackParamList } from "../types/navigation";

type Props = NativeStackScreenProps<HomeStackParamList, "GreenhouseDetail">;

type MetricKey = "air_temperature" | "air_humidity" | "soil_moisture";

const METRICS: Record<
  MetricKey,
  { label: string; title: string; unit: string; color: string }
> = {
  air_temperature: {
    label: "Повітря",
    title: "Температура повітря",
    unit: "°",
    color: "#586E5A",
  },
  air_humidity: {
    label: "Волога",
    title: "Вологість повітря",
    unit: "%",
    color: "#3b82f6",
  },
  soil_moisture: {
    label: "Ґрунт",
    title: "Вологість ґрунту",
    unit: "%",
    color: "#a16207",
  },
};

const METRIC_KEYS = Object.keys(METRICS) as MetricKey[];

export default function GreenhouseDetailScreen({ route, navigation }: Props) {
  const { nodeId } = route.params;
  const [metric, setMetric] = useState<MetricKey>("air_temperature");
  const meta = METRICS[metric];

  const { data: greenhouses } = useGreenhouses();
  const config = greenhouses?.find((g) => g.node_id === nodeId);

  const since = useMemo(
    () => new Date(Date.now() - 24 * 60 * 60 * 1000),
    [],
  );

  const { data: buckets } = useAggregate(nodeId, {
    bucketMinutes: 15,
    since,
  });

  const goToSettings = useCallback(() => {
    navigation.getParent()?.navigate("Settings" as never);
  }, [navigation]);

  useLayoutEffect(() => {
    navigation.setOptions({
      title: config?.label ?? "Теплиця",
      headerTitleAlign: "left",
      headerRight: () => (
        <Pressable onPress={goToSettings} className="px-2">
          <Ionicons name="settings-sharp" size={22} color="#586E5A" />
        </Pressable>
      ),
    });
  }, [navigation, config?.label, goToSettings]);

  const thresholds = config?.thresholds[metric];

  const chartData = (buckets ?? []).map((b) => ({
    x: new Date(b.bucket).getTime(),
    y: b[metric].avg,
  }));

  const stats = summarizeBuckets(buckets ?? [], metric, thresholds);

  const goToNext = () => {
    const idx = METRIC_KEYS.indexOf(metric);
    if (idx < METRIC_KEYS.length - 1) setMetric(METRIC_KEYS[idx + 1]);
  };

  const goToPrev = () => {
    const idx = METRIC_KEYS.indexOf(metric);
    if (idx > 0) setMetric(METRIC_KEYS[idx - 1]);
  };

  const swipe = Gesture.Pan()
    .activeOffsetX([-20, 20])
    .failOffsetY([-20, 20])
    .onEnd((e) => {
      if (e.translationX < -50) {
        runOnJS(goToNext)();
      } else if (e.translationX > 50) {
        runOnJS(goToPrev)();
      }
    });

  return (
    <Screen topInset={false}>
      <View className="flex-row bg-stone-100 p-1 rounded-lg mb-3 gap-1">
        {METRIC_KEYS.map((k) => (
          <Pressable
            key={k}
            onPress={() => setMetric(k)}
            className={`flex-1 py-2 rounded-md ${
              metric === k ? "bg-white" : "bg-stone-200"
            }`}
          >
            <Text
              className={`text-center text-sm ${
                metric === k
                  ? "font-semibold text-brand"
                  : "text-stone-600"
              }`}
            >
              {METRICS[k].label}
            </Text>
          </Pressable>
        ))}
      </View>

      <GestureDetector gesture={swipe}>
        <View>
          <Text className="text-stone-500 text-sm mb-2">
            Останні 24 години
          </Text>

          <View className="bg-white p-4 rounded-lg mb-3">
            <Chart
              data={chartData}
              min={thresholds?.min}
              max={thresholds?.max}
              unit={meta.unit}
              color={meta.color}
            />
          </View>

          {stats && (
            <StatsCard
              title={meta.title}
              min={stats.min}
              max={stats.max}
              avg={stats.avg}
              unit={meta.unit}
              inRangePct={stats.inRangePct ?? undefined}
            />
          )}
        </View>
      </GestureDetector>
    </Screen>
  );
}
