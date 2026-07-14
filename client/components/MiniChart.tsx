import { View } from "react-native";
import { CartesianChart, Line, Area } from "victory-native";

type Props = {
  data: { x: number; y: number }[];
  color?: string;
  height?: number;
}

export default function MiniChart({
  data,
  color = "#586E5A",
  height = 70,
}: Props) {
  return (
    <View style={{ height }}>
      <CartesianChart
        data={data}
        xKey="x"
        yKeys={["y"]}
        domainPadding={{ top: 6, bottom: 6 }}
      >
        {({ points }) => (
          //<Line points={points.y} color={color} strokeWidth={2} />
          <Area points={points.y} y0={0} color={color}  />
        )}

      </CartesianChart>
    </View>
  )
}
