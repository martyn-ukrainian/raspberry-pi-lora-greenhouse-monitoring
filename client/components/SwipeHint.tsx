import { useEffect } from "react";
import { View } from "react-native";
import Animated, {
  useSharedValue,
  useAnimatedStyle,
  withRepeat,
  withSequence,
  withTiming,
  withDelay,
  Easing,
} from "react-native-reanimated";
import { Ionicons } from "@expo/vector-icons";

type Props = {
  hasMore?: boolean;
};

export default function SwipeHint({ hasMore = true }: Props) {
  const translateY = useSharedValue(0);
  const opacity = useSharedValue(0);

  useEffect(() => {
    if (!hasMore) return;

    opacity.value = withTiming(1, { duration: 400 });

    translateY.value = withRepeat(
      withSequence(
        withTiming(10, { duration: 700, easing: Easing.inOut(Easing.quad) }),
        withTiming(0, { duration: 700, easing: Easing.inOut(Easing.quad) }),
      ),
      4,
      false,
    );

    opacity.value = withDelay(4500, withTiming(0, { duration: 600 }));
  }, [hasMore, opacity, translateY]);

  const style = useAnimatedStyle(() => ({
    opacity: opacity.value,
    transform: [{ translateY: translateY.value }],
  }));

  if (!hasMore) return null;

  return (
    <View
      pointerEvents="none"
      style={{
        position: "absolute",
        bottom: 24,
        left: 0,
        right: 0,
        alignItems: "center",
      }}
    >
      <Animated.View style={style}>
        <Ionicons name="chevron-down" size={28} color="#586E5A" />
      </Animated.View>
    </View>
  );
}
