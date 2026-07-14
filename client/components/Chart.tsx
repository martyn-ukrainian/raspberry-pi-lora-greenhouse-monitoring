import { useMemo } from "react";
import { View, Platform } from "react-native";
import { CartesianChart, Line } from "victory-native";
import {
  matchFont,
  Line as SkiaLine,
  Rect,
  DashPathEffect,
} from "@shopify/react-native-skia";

type Props = {
  data: { x: number; y: number }[];
  color?: string;
  height?: number;
  min?: number | null;
  max?: number | null;
  unit?: string;
};

const font = matchFont({
  fontFamily: Platform.select({ ios: "Helvetica", default: "serif" }),
  fontSize: 12,
});

export default function Chart({
  data,
  color = "#586E5A",
  height = 240,
  min,
  max,
  unit = "",
}: Props) {
  const yDomain = useMemo(() => {
    if (data.length === 0) return undefined;
    const ys = data.map((d) => d.y);
    const bounds = [...ys];
    if (min != null) bounds.push(min);
    if (max != null) bounds.push(max);
    const lo = Math.min(...bounds);
    const hi = Math.max(...bounds);
    const pad = (hi - lo) * 0.05 || 1;
    return [lo - pad, hi + pad] as [number, number];
  }, [data, min, max]);

  return (
    <View style={{ height }}>
      <CartesianChart
        data={data}
        xKey="x"
        yKeys={["y"]}
        domain={yDomain ? { y: yDomain } : undefined}
        domainPadding={{ top: 12, bottom: 12, left: 8, right: 8 }}
        axisOptions={{
          font,
          tickCount: { x: 5, y: 5 },
          lineWidth: { grid: { x: 0, y: 1 }, frame: 0 },
          lineColor: {
            grid: { x: "transparent", y: "#f0f0ee" },
            frame: "transparent",
          },
          labelColor: "#78716c",
          formatXLabel: (ms) =>
            new Date(ms).toLocaleTimeString("uk-UA", {
              hour: "2-digit",
              minute: "2-digit",
            }),
          formatYLabel: (v) => `${v.toFixed(0)}${unit}`,
        }}
      >
        {({ points, chartBounds, yScale }) => {
          const chartWidth = chartBounds.right - chartBounds.left;

          return (
            <>
              {max != null && (
                <Rect
                  x={chartBounds.left}
                  y={chartBounds.top}
                  width={chartWidth}
                  height={yScale(max) - chartBounds.top}
                  color="rgba(239, 68, 68, 0.15)"
                />
              )}

              {min != null && (
                <Rect
                  x={chartBounds.left}
                  y={yScale(min)}
                  width={chartWidth}
                  height={chartBounds.bottom - yScale(min)}
                  color="rgba(59, 130, 246, 0.15)"
                />
              )}

              {min != null && (
                <SkiaLine
                  p1={{ x: chartBounds.left, y: yScale(min) }}
                  p2={{ x: chartBounds.right, y: yScale(min) }}
                  color="#3b82f6"
                  strokeWidth={1}
                >
                  <DashPathEffect intervals={[6, 4]} />
                </SkiaLine>
              )}

              {max != null && (
                <SkiaLine
                  p1={{ x: chartBounds.left, y: yScale(max) }}
                  p2={{ x: chartBounds.right, y: yScale(max) }}
                  color="#ef4444"
                  strokeWidth={1}
                >
                  <DashPathEffect intervals={[6, 4]} />
                </SkiaLine>
              )}

              <Line points={points.y} color={color} strokeWidth={2.5} />
            </>
          );
        }}
      </CartesianChart>
    </View>
  );
}
