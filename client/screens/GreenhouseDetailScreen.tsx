import {
  useCallback,
  useLayoutEffect,
  useMemo,
  useRef,
  useState,
} from "react";
import { View, Text, Pressable, useWindowDimensions } from "react-native";
import { SafeAreaView } from "react-native-safe-area-context";
import PagerView, {
  type PagerViewOnPageSelectedEvent,
} from "react-native-pager-view";
import { TabView, type SceneRendererProps } from "react-native-tab-view";
import { Ionicons } from "@expo/vector-icons";
import type { NativeStackScreenProps } from "@react-navigation/native-stack";

import Chart from "../components/Chart";
import StatsCard from "../components/StatsCard";
import SwipeHint from "../components/SwipeHint";
import { useGreenhouses } from "../hooks/useGreenhouses";
import { useAggregate } from "../hooks/useAggregate";
import { summarizeBuckets } from "../utils/stats";
import type { Greenhouse } from "../types";
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

type Route = { key: MetricKey; title: string };

const ROUTES: Route[] = METRIC_KEYS.map((k) => ({
  key: k,
  title: METRICS[k].label,
}));

type MetricSceneProps = {
  metric: MetricKey;
  nodeId: string;
  since: Date;
  config: Greenhouse | undefined;
};

function MetricScene({ metric, nodeId, since, config }: MetricSceneProps) {
  const { data: buckets } = useAggregate(nodeId, {
    bucketMinutes: 15,
    since,
  });

  const meta = METRICS[metric];
  const thresholds = config?.thresholds[metric];

  const chartData = (buckets ?? []).map((b) => ({
    x: new Date(b.bucket).getTime(),
    y: b[metric].avg,
  }));

  const stats = summarizeBuckets(buckets ?? [], metric, thresholds);

  return (
    <View className="flex-1 p-2">
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
  );
}

type GreenhousePageProps = {
  greenhouse: Greenhouse;
  since: Date;
};

function GreenhousePage({ greenhouse, since }: GreenhousePageProps) {
  const [index, setIndex] = useState(0);
  const layout = useWindowDimensions();

  const renderScene = ({
    route: r,
  }: SceneRendererProps & { route: Route }) => (
    <MetricScene
      metric={r.key}
      nodeId={greenhouse.node_id}
      since={since}
      config={greenhouse}
    />
  );

  const renderTabBar = () => (
    <View className="px-2 pt-2">
      <Text className="text-stone-500 text-sm mb-2">Останні 24 години</Text>
      <View className="flex-row bg-stone-100 p-1 rounded-lg gap-1">
        {ROUTES.map((r, i) => (
          <Pressable
            key={r.key}
            onPress={() => setIndex(i)}
            className={`flex-1 py-2 rounded-md ${
              i === index ? "bg-white" : "bg-stone-200"
            }`}
          >
            <Text
              className={`text-center text-sm ${
                i === index
                  ? "font-semibold text-brand"
                  : "text-stone-600"
              }`}
            >
              {r.title}
            </Text>
          </Pressable>
        ))}
      </View>
    </View>
  );

  return (
    <TabView
      navigationState={{ index, routes: ROUTES }}
      renderScene={renderScene}
      onIndexChange={setIndex}
      renderTabBar={renderTabBar}
      initialLayout={{ width: layout.width }}
      lazy
    />
  );
}

export default function GreenhouseDetailScreen({
  route,
  navigation,
}: Props) {
  const { nodeId } = route.params;
  const pagerRef = useRef<PagerView>(null);
  const { data: greenhouses } = useGreenhouses();

  const currentIndex = greenhouses?.findIndex((g) => g.node_id === nodeId) ?? 0;
  const config = greenhouses?.[currentIndex];

  // Extended list for infinite loop: [last, ...all, first].
  // Real greenhouse i lives at extended index i + 1.
  const extended = useMemo(() => {
    if (!greenhouses || greenhouses.length === 0) return [];
    const last = greenhouses[greenhouses.length - 1];
    const first = greenhouses[0];
    return [last, ...greenhouses, first];
  }, [greenhouses]);

  const since = useMemo(
    () => new Date(Date.now() - 24 * 60 * 60 * 1000),
    [],
  );

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

  const handlePageSelected = useCallback(
    (e: PagerViewOnPageSelectedEvent) => {
      if (!greenhouses || greenhouses.length === 0) return;
      const pagerIdx = e.nativeEvent.position;
      const total = greenhouses.length;

      // Landed on the leading duplicate (copy of last) — silently jump
      // to the real last page. Next scroll then lands on real greenhouses.
      if (pagerIdx === 0) {
        pagerRef.current?.setPageWithoutAnimation(total);
        const newId = greenhouses[total - 1].node_id;
        if (newId !== nodeId) navigation.setParams({ nodeId: newId });
        return;
      }

      // Landed on the trailing duplicate (copy of first) — jump to real first.
      if (pagerIdx === total + 1) {
        pagerRef.current?.setPageWithoutAnimation(1);
        const newId = greenhouses[0].node_id;
        if (newId !== nodeId) navigation.setParams({ nodeId: newId });
        return;
      }

      // Normal page — realIdx is offset by the leading duplicate.
      const realIdx = pagerIdx - 1;
      const newId = greenhouses[realIdx].node_id;
      if (newId !== nodeId) navigation.setParams({ nodeId: newId });
    },
    [greenhouses, nodeId, navigation],
  );

  if (!greenhouses || greenhouses.length === 0) {
    return <SafeAreaView edges={[]} className="flex-1 bg-stone-100" />;
  }

  return (
    <SafeAreaView edges={[]} className="flex-1 bg-stone-100">
      <PagerView
        ref={pagerRef}
        orientation="vertical"
        initialPage={currentIndex + 1}
        onPageSelected={handlePageSelected}
        style={{ flex: 1 }}
        offscreenPageLimit={1}
      >
        {extended.map((g, i) => (
          <View key={`${g.node_id}-${i}`}>
            <GreenhousePage greenhouse={g} since={since} />
          </View>
        ))}
      </PagerView>

      <SwipeHint hasMore={greenhouses.length > 1} />
    </SafeAreaView>
  );
}
