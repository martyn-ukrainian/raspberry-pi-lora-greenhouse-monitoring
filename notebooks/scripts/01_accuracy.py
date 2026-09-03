import pandas as pd
from pathlib import Path

path = Path(__file__).parent.parent / "data" / "measurement.csv"
df = pd.read_csv(path, parse_dates=["timestamp"])

ref = df[(df["node_id"] == "stage3-reference") & (df["timestamp"] >= "2026-08-25")]
low = df[(df["node_id"] == "stage3-lowpower") & (df["timestamp"] >= "2026-08-25")]


print(ref.shape)
print(low.shape)

ref_end = ref["timestamp"].max()
print(ref_end)

ref = ref.sort_values(by="timestamp")

ref_gaps = ref["timestamp"].diff().dt.total_seconds()
print(ref_gaps.median())
print(ref_gaps.max())
ref_gap = ref["timestamp"].diff().dt.total_seconds()
ref_new_burst = ref_gap > 45
ref_burst_id = ref_new_burst.cumsum()
print(ref_burst_id)

ref = ref.assign(burst_id=ref_burst_id)
sizes = ref.groupby("burst_id").size()

print(f"burst sizes: {sizes.value_counts()}")



low_to_ref = low[low["timestamp"] <= ref_end]
print(low.shape)
low_to_ref = low_to_ref.sort_values(by="timestamp")
low_to_ref_gaps = low_to_ref["timestamp"].diff().dt.total_seconds()
print(low_to_ref_gaps.median())
print(low_to_ref_gaps.max())

low_to_ref_new_burst = low_to_ref_gaps > 45
low_to_ref_burst_id = low_to_ref_new_burst.cumsum()
low_to_ref = low_to_ref.assign(burst_id=low_to_ref_burst_id)
low_to_ref_sizes = low_to_ref.groupby("burst_id").size()

print(f"low burst sizes: {low_to_ref_sizes.value_counts()}")


low = low.sort_values(by="timestamp")
low_gaps = low["timestamp"].diff().dt.total_seconds()

print(low_gaps.max())

low_new_burst = low_gaps > 45
low_burst_id = low_new_burst.cumsum()
low = low.assign(burst_id=low_burst_id)
low_sizes = low.groupby("burst_id").size()

print(f"low burst sizes: {low_sizes.value_counts()}")
print(low["air_temperature"].isna().sum())

air_temp_na = low["air_temperature"].isna()

full_burst_sum = low_sizes[low_sizes == 7].sum()
low_sizes_sum = low_sizes.sum()
print(f"full burst sum: {full_burst_sum}")
print(f"low burst sum: {low_sizes_sum}")

print(air_temp_na.sum() / low_sizes_sum * 100)
print((low_sizes_sum - full_burst_sum) / low_sizes_sum * 100)

##############################################################
#
#

diffs = ref["soil_raw"].diff()
diffs_abs = diffs.abs()
diffs_nonzero = diffs_abs[diffs_abs != 0]
print(diffs_nonzero)
print(diffs_nonzero.min(), diffs_nonzero.max(), diffs_nonzero.mean())


low_diffs = low["soil_raw"].diff()
low_diffs_abs = low_diffs.abs()
low_diffs_nonzero = low_diffs_abs[low_diffs_abs != 0]
print(low_diffs_nonzero)
print(low_diffs_nonzero.min(), low_diffs_nonzero.max(), low_diffs_nonzero.mean())

###############################################################
#
#

ref_no_gap = ref_gap <= 45
diffs_clean = diffs[ref_no_gap]
noise_floor = diffs_clean.std() / (2 ** 0.5)
print(f"Ref soil raw noise_floor: {noise_floor}")

noise_floor_low = low_diffs.std() / (2 ** 0.5)
print(f"Low soil raw noise_floor: {noise_floor_low}")

same_burst = low["burst_id"] == low["burst_id"].shift(1)
low_diffs_inburst = low_diffs[same_burst]
low_noise_floor_inburst = low_diffs_inburst.std() / (2 ** 0.5)
print(f"Low soil raw noise_floor in burst: {low_noise_floor_inburst}")

low["pos_in_burst"] = low.groupby("burst_id").cumcount()
print(low["pos_in_burst"])

in_settled_region = same_burst & (low["pos_in_burst"] >= 2)

low_diffs_settled = low_diffs[in_settled_region]
low_noise_floor_settled = low_diffs_settled.std() / (2 ** 0.5)
print(f"Low soil raw noise_floor in settled region: {low_noise_floor_settled}")
