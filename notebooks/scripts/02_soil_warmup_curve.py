import pandas as pd
from pathlib import Path
import matplotlib.pyplot as plt


path = Path(__file__).parent.parent / "data" / "soil_warmup_curve.csv"
df = pd.read_csv(path)

print(df.head())
print(df.dtypes)
print(df.shape)

df["ms"] = pd.to_numeric(df["ms"], errors="coerce")
df["raw"] = pd.to_numeric(df["raw"], errors="coerce")

df = df.dropna()
df["ms"] = df["ms"].astype(int)
df["raw"] = df["raw"].astype(int)

ms_diff = df["ms"].diff() < 0
df["run_id"] = ms_diff.cumsum()

print(df.head())
print(df.dtypes)
print(df.shape)

print(df.groupby("run_id").size())


df_runs = df[df["ms"] > 5000].groupby("run_id")
plateau = df_runs["raw"].median()

print(plateau)

df["plateau"] = df["run_id"].map(plateau)
df["dev"] = df["raw"] - df["plateau"]
print(df.head())


########### 4
#

plt.figure(figsize=(10, 5))

run0 = df[df["run_id"] == 0]
run1 = df[df["run_id"] == 1]

plt.plot(run0["ms"], run0["dev"], label="прогін 1")
plt.plot(run1["ms"], run1["dev"], label="прогін 2")

plt.xscale("log")
plt.xlabel("ms")
plt.ylabel("dev")
plt.title("Soil Warmup Curve")
plt.legend()
plt.show()
