import { useMemo } from "react";
import { View, Platform } from "react-native";
import { CartesianChart, Line, Area } from "victory-native";
import { matchFont, Line as SkiaLine, DashPathEffect } from "@shopify/react-native-skia";

type Props = {
  data: { x: number; y: number }[];
  color?: string;
  height?: number;
  min?: number | null;
  max?: number | null;
}

const font = matchFont({
  fontFamily: Platform.select({ ios: "Helvetica", default: "serif" }),
  fontSize: 11,
})

export default function MiniChart({
  data,
  color = "#586E5A",
  height = 100,
  min,
  max,
}: Props) {
  const yDomain = useMemo(() => {
    if (data.length === 0) return undefined;
    const ys = data.map((d) => d.y);
    const bounds = [...ys];
    if (min != null) bounds.push(min);
    if (max != null) bounds.push(max);
    return [Math.min(...bounds), Math.max(...bounds)] as [number, number];
  }, [data, min, max]);

  return (
    <View style={{ height }}>
      <CartesianChart
        data={data}
        xKey="x"
        yKeys={["y"]}
        domain={yDomain ? { y: yDomain } : undefined}
        domainPadding={{ top: 4, bottom: 4, left: 4, right: 4 }}
        axisOptions={{
          font,
          tickCount: { x: 6, y: 4 },
          lineWidth: 0,
          labelColor: "#a8a29e",
          formatXLabel: (ms) =>
          new Date(ms).toLocaleTimeString("uk-UA", {
            hour: "2-digit",
            minute: "2-digit",
          }),
          formatYLabel: (v) => `${v.toFixed(0)}°`
        }}
      >
        {({ points, chartBounds, yScale }) => (
          <>

            <Line points={points.y} color={color} strokeWidth={2} />
            {min != null && (
              <SkiaLine
                p1={{ x: chartBounds.left, y: yScale(min) }}
                p2={{ x: chartBounds.right, y: yScale(min) }}
                color="#3b82f6"
                strokeWidth={1}
              >
                <DashPathEffect intervals={[4, 4]} />
              </SkiaLine>
            )}

            {max != null && (
              <SkiaLine
                p1={{ x: chartBounds.left, y: yScale(max) }}
                p2={{ x: chartBounds.right, y: yScale(max) }}
                color="#ef4444"
                strokeWidth={1}
              >
                <DashPathEffect intervals={[4, 4]} />
              </SkiaLine>
            )}
          </>
          // <Area points={points.y} y0={0} color={color}  />
        )}

      </CartesianChart>
    </View>
  )
}
