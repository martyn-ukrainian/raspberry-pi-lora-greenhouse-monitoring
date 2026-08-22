import { useEffect, useState } from "react";
import { getGreenhouses, getAggregate, type Greenhouse, type AggregateBucket } from "./api";
import "./App.css";

function formatTime(iso: string): string {
  return new Date(iso).toLocaleTimeString("uk-UA", { hour: "2-digit", minute: "2-digit" });
}

export default function App() {
  const [greenhouses, setGreenhouses] = useState<Greenhouse[] | null>(null);
  const [buckets, setBuckets] = useState<AggregateBucket[] | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    getGreenhouses().then(setGreenhouses).catch((e) => setError(String(e)));
  }, []);

  useEffect(() => {
    const nodeId = greenhouses?.[0]?.node_id;
    if (!nodeId) return;

    const load = () => getAggregate(nodeId).then(setBuckets).catch((e) => setError(String(e)));
    load();
    const interval = setInterval(load, 30_000);
    return () => clearInterval(interval);
  }, [greenhouses]);

  if (error) {
    return <main className="status">Помилка: {error}</main>;
  }

  if (!greenhouses || !buckets) {
    return <main className="status">Завантаження...</main>;
  }

  const label = greenhouses[0]?.label ?? "—";
  const latest = buckets[buckets.length - 1];

  return (
    <main>
      <h1>{label}</h1>

      {!latest ? (
        <p className="status">Даних ще нема</p>
      ) : (
        <>
          <p className="updated">Оновлено: {formatTime(latest.bucket)}</p>
          <div className="cards">
            <div className="card">
              <span className="value">{latest.air_temperature.avg.toFixed(1)}°</span>
              <span className="label">Температура повітря</span>
            </div>
            <div className="card">
              <span className="value">{latest.air_humidity.avg.toFixed(0)}%</span>
              <span className="label">Вологість повітря</span>
            </div>
            <div className="card">
              <span className="value">{latest.soil_moisture.avg.toFixed(0)}%</span>
              <span className="label">Вологість ґрунту</span>
            </div>
          </div>
        </>
      )}
    </main>
  );
}
