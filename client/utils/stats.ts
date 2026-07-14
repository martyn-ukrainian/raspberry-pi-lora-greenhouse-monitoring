import type { AggregateBucket, SensorStats } from "../types";

type MetricKey = "air_temperature" | "air_humidity" | "soil_moisture";

export type BucketStats = {
  min: number;
  max: number;
  avg: number;
  inRangePct: number | null;
};

/**
 * Reduces an array of buckets down to a single stats summary for one metric.
 * - `min` / `max` — extremes across the whole range.
 * - `avg` — count-weighted so partial edge buckets don't skew the mean.
 * - `inRangePct` — % of buckets whose min/max fit inside the given thresholds.
 *   Null when thresholds are missing or there is no data.
 */
export function summarizeBuckets(
  buckets: AggregateBucket[],
  metric: MetricKey,
  thresholds?: { min: number | null; max: number | null },
): BucketStats | null {
  if (buckets.length === 0) return null;

  let min = Infinity;
  let max = -Infinity;
  let weightedSum = 0;
  let totalCount = 0;
  let inRangeCount = 0;

  for (const b of buckets) {
    const s: SensorStats = b[metric];
    if (s.min < min) min = s.min;
    if (s.max > max) max = s.max;
    weightedSum += s.avg * b.count;
    totalCount += b.count;

    if (thresholds) {
      const okMin = thresholds.min == null || s.min >= thresholds.min;
      const okMax = thresholds.max == null || s.max <= thresholds.max;
      if (okMin && okMax) inRangeCount += 1;
    }
  }

  const hasThresholds =
    thresholds != null && (thresholds.min != null || thresholds.max != null);

  return {
    min,
    max,
    avg: totalCount > 0 ? weightedSum / totalCount : 0,
    inRangePct: hasThresholds ? (inRangeCount / buckets.length) * 100 : null,
  };
}
