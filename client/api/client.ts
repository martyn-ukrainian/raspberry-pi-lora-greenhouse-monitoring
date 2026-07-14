const API_BASE = process.env.EXPO_PUBLIC_API_URL;

if (!API_BASE) {
  throw new Error(
    "EXPO_PUBLIC_API_URL is not set. Copy client/.env.example to client/.env.local and set your LAN IP.",
  );
}

async function requestMiddleware(url: string, options: RequestInit): Promise<void> {
  console.log(`-> ${options.method || "GET"} ${url}`);
}

async function responseMiddleware(response: Response): Promise<void> {
  console.log(`<- ${response.status} ${response.url}`)
}

export async function apiClient<T>(
  path: string,
  options: RequestInit = {}
): Promise<T> {
  const url = `${API_BASE}${path}`;
  const finalOptions: RequestInit = {
    ...options,
    headers: {
      "Content-Type": "application/json",
      ...options.headers,
    },
  }

  await requestMiddleware(url, finalOptions);
  const response = await fetch(url, finalOptions);
  await responseMiddleware(response);

  if (!response.ok) {
    throw new Error(`HTTP ${response.status}: ${response.statusText}`);
  }

  return response.json();
}
