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
  return (
    <View style={{ height }}>
      <CartesianChart
        data={data}
        xKey="x"
        yKeys={["y"]}
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
