import { View, Text, Pressable } from "react-native";

export type Period = "1h" | "6h" | "24h" | "3d" | "7d";

export const PERIODS: Record<
  Period,
  { label: string; hours: number; bucketMinutes: number; description: string }
> = {
  "1h":  { label: "1г",  hours: 1,   bucketMinutes: 1,   description: "Остання година" },
  "6h":  { label: "6г",  hours: 6,   bucketMinutes: 5,   description: "Останні 6 годин" },
  "24h": { label: "24г", hours: 24,  bucketMinutes: 15,  description: "Останні 24 години" },
  "3d":  { label: "3д",  hours: 72,  bucketMinutes: 60,  description: "Останні 3 дні" },
  "7d":  { label: "Тиж", hours: 168, bucketMinutes: 180, description: "Останній тиждень" },
};

const PERIOD_KEYS = Object.keys(PERIODS) as Period[];

type Props = {
  value: Period;
  onChange: (period: Period) => void;
};

export default function PeriodSelector({ value, onChange }: Props) {
  return (
    <View className="flex-row bg-stone-100 p-1 rounded-lg gap-1">
      {PERIOD_KEYS.map((p) => {
        const active = p === value;
        return (
          <Pressable
            key={p}
            onPress={() => onChange(p)}
            className={`flex-1 py-1.5 rounded-md ${
              active ? "bg-white" : "bg-stone-200"
            }`}
          >
            <Text
              className={`text-center text-xs ${
                active ? "font-semibold text-brand" : "text-stone-600"
              }`}
            >
              {PERIODS[p].label}
            </Text>
          </Pressable>
        );
      })}
    </View>
  );
}
