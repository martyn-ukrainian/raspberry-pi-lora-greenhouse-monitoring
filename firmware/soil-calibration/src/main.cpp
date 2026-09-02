// Калібрувальний стенд ґрунтових сенсорів.
//
// Задача, під яку писалось: відро ґрунту, чотири ємнісні сенсори в ньому
// (2× v1.2 TENSTAR і 2× "corrosion resistant" з резерву), і вода, яка
// доливається порціями по 20-50 мл. Треба побачити, як кожен сенсор реагує
// на однакову дозу — і чи взагалі реагує однаково.
//
// Тому тут навмисно НЕМА нічого з бойового вузла:
//   - ні LoRa: дані їдуть по USB, приймає їх ноут;
//   - ні deep sleep: живлення від USB, міряємо безперервно;
//   - ні soilRawToPercent(): відсотки — це вже результат калібрування, а ми
//     його якраз і збираємось порахувати. У порт іде сире ADC і мілівольти.
//
// Формат виводу — CSV з рядками-коментарями (`#`), один рядок на замір.
// Порції води позначаються командою прямо в Serial, тож момент доливу
// потрапляє в ті самі дані, а не в окремий блокнот. Пише файл host-скрипт
// tools/soil_log.py; рахує підсумок tools/soil_summary.py. Уся процедура —
// README.md поруч.

#include <Arduino.h>
#include <SSD1306Wire.h>
#include <Adafruit_SHT31.h>
#include <esp_system.h>

// Пінаут Heltec WiFi LoRa 32 V3 — той самий, що в решті прошивок.
#define OLED_SDA   17
#define OLED_SCL   18
#define OLED_RST   21
#define VEXT_CTRL  36

#define AIR_SDA    41
#define AIR_SCL    42

// Вбудований дільник вимірювання батареї: GPIO37 вмикає його (LOW = on),
// GPIO1 читає. Тримати ввімкненим постійно не можна — він сам висаджує
// батарею, тому вмикається лише на час заміру.
//
// GPIO1 вільний: ґрунтові канали сидять на GPIO2..GPIO5.
#define VBAT_ADC   1
#define VBAT_CTRL  37

// Номінал дільника для V3. Звірити мультиметром на своїй платі:
// VBAT_DIVIDER = 4.9 x (реальна напруга / показана).
#define VBAT_DIVIDER 4.9f

// Раз на 30 с досить: батарея не змінюється за секунду, а кожен замір це
// ще й вмикання дільника.
#define VBAT_PERIOD_MS 30000

// Чотири сенсори замість трьох: GPIO2/3/4 — як у вузлі, плюс GPIO5. Усі
// чотири на ADC1 (на ESP32-S3 це GPIO1..GPIO10), і це не дрібниця: ADC2 на
// цій платі ділиться з Wi-Fi і при його вмиканні просто перестає читати.
#define SOIL_1  2
#define SOIL_2  3
#define SOIL_3  4
#define SOIL_4  5

// Скільки каналів реально задіяно. Число має відповідати ЗАЛІЗУ, а не
// кількості пінів: непідключений ADC-вхід не читає нуль — він висить і віддає
// шум у десятки мілівольт.
//
// Шкодить це не лише зайвими стовпцями. Порожній канал дає ~30 мВ, живий —
// ~880, і графік мусить умістити обидва: вісь розтягується на 0-900, а вся
// цікава зміна живих сенсорів (одиниці мілівольт) перетворюється на пряму
// лінію. Дані правильні, масштаб — ні.
//
// Задається збіркою: -DSOIL_COUNT=4, коли підключиш решту.
#ifndef SOIL_COUNT
#define SOIL_COUNT 2
#endif

#if SOIL_COUNT < 1 || SOIL_COUNT > 4
#error "SOIL_COUNT: від 1 до 4 — стільки пінів заведено в SOIL_PINS"
#endif

const int SOIL_PINS[4] = {SOIL_1, SOIL_2, SOIL_3, SOIL_4};

// Мітки їдуть у заголовок CSV і в labels[] пакета — це імена стовпців у
// майбутній таблиці.
//
// Перші дві названі за сенсором, бо він там стоїть постійно. Останні дві —
// ПОЗИЦІЙНО, за номером піна, і це навмисно: канал не змінюється ніколи, а
// сенсор у ньому — постійно. Мітка, названа за сенсором, застаріває при
// першій же заміні й починає тихо брехати.
//
// Саме так і сталось: до 2026-08-23 тут стояли "v12a"/"v12b" для GPIO2/GPIO3,
// хоча фізично там уже добу були v2.0. Дані писались із правильних пінів,
// але під чужими іменами. Історія розкладки — docs/довідка/канали-і-мітки.md.
const char *SOIL_LABELS[4] = {"v20a", "v20b", "ch4", "ch5"};

// ADC ESP32 шумить одиницями молодших розрядів, а нам треба ловити різницю
// від 20 мл води — тобто десятки одиниць. 32 заміри на сенсор коштують
// ~1 мс і прибирають шум, не з'їдаючи період.
#define SOIL_OVERSAMPLE 32

#define SAMPLE_MS_DEFAULT 1000

// Ємнісний сенсор віддає напругу з пікового детектора, і його конденсатор
// заряджається не миттєво — деталі в greenhouse-node-lowpower/README.md.
// Тут живлення постійне (USB), тож прогрів потрібен рівно один раз на старті.
#define SOIL_WARMUP_MS 5000

// Прогрів міряється ТІЛЬКИ справжнім зняттям живлення: 555 має стартувати з
// нуля, а конденсатор пікового детектора — зарядитись. Перетикання сигнального
// дроту цього не робить взагалі, тому шина Ve тут не зручність, а умова.
// Пауза за замовчуванням коротка, бо це швидка перевірка. Але для висновку
// про батарейний вузол вона НЕ годиться: там сенсор лежить знеструмлений 15
// хвилин, і конденсатор детектора встигає розрядитись значно глибше. Пауза
// задається другим аргументом команди саме тому, що від неї залежить, чи
// можна переносити результат на бойовий режим.
#define PROBE_OFF_MS 3000       // типова пауза, якщо не сказано інше
#define PROBE_PERIOD_MS 100     // крок кривої: 0,1 с
#define PROBE_DEFAULT_S 30

#define CMD_MAX_LEN 64



// Чи відповів SHT31 на шині. Не константа: сенсор можна підключити до вже
// запущеної плати, і прошивка має його підхопити без перезавантаження —
// у полі перепрошивати заради одного дроту незручно.
bool airPresent = false;
unsigned long airRetryAt = 0;

// Скільки читань поспіль дали NAN. Сенсор може відпасти вже ПІСЛЯ вдалого
// старту — дюпон зачепили, шлейф ворухнули, — і тоді begin() більше ніколи
// не викликається, а плата читає NAN до перезавантаження.
//
// Саме так і сталось 2026-08-23: SHT31 працював на старті, відпав через
// кілька хвилин під час підключення батареї, і колонки air_* лишились
// порожніми назавжди. У полі це означає втрачену температуру за весь прогін.
int airFailStreak = 0;
#define AIR_FAIL_LIMIT 5

SSD1306Wire display(0x3c, OLED_SDA, OLED_SCL);

TwoWire airWire = TwoWire(1);
Adafruit_SHT31 sht31 = Adafruit_SHT31(&airWire);

unsigned long samplePeriodMs = SAMPLE_MS_DEFAULT;
unsigned long runStartMs = 0;
unsigned long nextSampleAt = 0;

// Скільки води влито від початку прогону, наростаючим підсумком. Саме ця
// колонка потім стає віссю X графіка "вода -> показник сенсора".
float waterMl = 0;

// Подія чіпляється до наступного рядка й одразу гаситься: в CSV вона має
// стояти рівно на тому замірі, коли сталась, а не тягнутись далі.
String pendingEvent = "";

String cmdBuf = "";

// OLED живиться з тієї самої шини Ve, що й сенсори. Малювати на знеструмлений
// екран не можна: I2C-запис не отримує ACK і блокує цикл, тобто плата
// перестає віддавати заміри рівно тоді, коли ти зняв живлення руками.
bool vePowered = true;

int lastRaw[4];
int lastMv[4];
float lastAirT = NAN;
float lastAirH = NAN;

// Напруга батареї й груба оцінка залишку. NAN, поки не заміряно, і лишається
// NAN на USB без батареї — "не знаємо" це не "нуль", те саме правило, що для
// повітря.
float lastVbat = NAN;
int lastBatPct = -1;
unsigned long vbatNextAt = 0;

// Напрямок зміни напруги: росте -> заряджається, падає -> розряджається.
//
// Окремого сигналу від зарядника плата не дає, тож визначаємо за поведінкою.
// Поріг 5 мВ за вікно: менше — це шум ADC, і індикатор блимав би туди-сюди.
// Вікно ~2,5 хв (п'ять замірів по 30 с) — заряджання за цей час дає десятки
// мілівольт, а розряд у роботі десь стільки ж, тільки вниз.
#define VBAT_TREND_N 5
#define VBAT_TREND_MV 5.0f
float vbatHistory[VBAT_TREND_N];
int vbatHistLen = 0;
int batCharging = 0;   // +1 заряджається, -1 розряджається, 0 незрозуміло

void updateBatteryTrend(float volts) {
  if (vbatHistLen < VBAT_TREND_N) {
    vbatHistory[vbatHistLen++] = volts;
  } else {
    for (int i = 1; i < VBAT_TREND_N; i++) vbatHistory[i - 1] = vbatHistory[i];
    vbatHistory[VBAT_TREND_N - 1] = volts;
  }
  if (vbatHistLen < VBAT_TREND_N) {
    batCharging = 0;
    return;
  }
  float delta = (vbatHistory[VBAT_TREND_N - 1] - vbatHistory[0]) * 1000.0f;
  batCharging = delta > VBAT_TREND_MV ? 1 : (delta < -VBAT_TREND_MV ? -1 : 0);
}

// Li-ion розряджається дуже нелінійно: від 4,2 до 3,7 В минає перша третина
// ємності, а від 3,7 до 3,4 — решта дві. Лінійна шкала по напрузі показувала б
// 50% там, де реально лишилось 15%.
//
// Тому не формула, а таблиця по точках типової кривої 18650 під малим
// навантаженням. Точність ±10% — для "скільки ще протримає" досить, для
// обліку енергії ні.
struct BatPoint { float volts; int pct; };
const BatPoint BAT_CURVE[] = {
    {4.20f, 100}, {4.10f, 92}, {4.00f, 85}, {3.90f, 74}, {3.80f, 60},
    {3.75f, 50},  {3.70f, 40}, {3.65f, 30}, {3.60f, 20}, {3.50f, 10},
    {3.40f, 5},   {3.30f, 0},
};

int batteryPercent(float volts) {
  if (isnan(volts)) return -1;
  const int n = sizeof(BAT_CURVE) / sizeof(BAT_CURVE[0]);
  if (volts >= BAT_CURVE[0].volts) return 100;
  if (volts <= BAT_CURVE[n - 1].volts) return 0;
  for (int i = 1; i < n; i++) {
    if (volts >= BAT_CURVE[i].volts) {
      const BatPoint &hi = BAT_CURVE[i - 1];
      const BatPoint &lo = BAT_CURVE[i];
      float k = (volts - lo.volts) / (hi.volts - lo.volts);
      return lo.pct + (int)(k * (hi.pct - lo.pct));
    }
  }
  return 0;
}

void readBattery() {
  if ((long)(millis() - vbatNextAt) < 0) return;
  vbatNextAt = millis() + VBAT_PERIOD_MS;

  // УВАГА до полярності: на цій ревізії Heltec V3 дільник вмикається ВИСОКИМ
// рівнем, а не низьким. Перевірено на залізі 2026-08-23:
//   CTRL=LOW  -> 0 мВ        (дільник вимкнений)
//   CTRL=HIGH -> 811 мВ      = 3,97 В на комірці
//
// Це протилежно до Vext_Ctrl (GPIO36), де LOW = увімкнено, і саме за
// аналогією з ним тут спершу стояв LOW. Наслідок був тихий: vbat завжди
// читався нулем, тобто прошивка мовчки вважала, що батареї нема.
  pinMode(VBAT_CTRL, OUTPUT);
  digitalWrite(VBAT_CTRL, HIGH);
  delay(10);

  analogSetPinAttenuation(VBAT_ADC, ADC_11db);
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(VBAT_ADC);
  mv /= 8;

  digitalWrite(VBAT_CTRL, LOW);   // дільник назад у вимкнений стан

  float volts = (mv * VBAT_DIVIDER) / 1000.0f;
  // Нижче цього батареї просто нема — плата на USB. Показувати 0% було б
  // брехнею: ми не розряджені, ми не на батареї.
  if (volts < 2.5f) {
    lastVbat = NAN;
    lastBatPct = -1;
    vbatHistLen = 0;
    batCharging = 0;
    return;
  }
  lastVbat = volts;
  lastBatPct = batteryPercent(volts);
  updateBatteryTrend(volts);
}

// ---------------------------------------------------------------------------
// Wi-Fi: передача пачками, з радіо вимкненим під час заміру
// ---------------------------------------------------------------------------
// Вмикається прапорцем -DAGRO_WIFI (середовище wifi_field). Без нього нижчий
// код не потрапляє у прошивку взагалі, і поведінка по USB не міняється ні на
// байт — USB лишається ОПОРНИМ трактом, з яким звіряють хмару.
//
// ГОЛОВНЕ РІШЕННЯ ТУТ — режим BURST. Передача Wi-Fi це імпульси 200-300 мА,
// які просаджують шину живлення, а від неї живиться ADC. Наш шум зараз
// σ = 0,6 мВ, і саме він дає роздільність 0,15 мл води — тобто просадка
// цілком може коштувати нам приладу.
//
// Тому за замовчуванням радіо ВИМКНЕНЕ під час замірів і вмикається лише на
// відправку пачки:
//
//     [Wi-Fi OFF] замір ×N  ->  [Wi-Fi ON] POST  ->  [OFF] ...
//
// Імпульси фізично рознесені із заміром у часі, тож потрапити в нього не
// можуть. Ціна — пауза на переконнект (1-3 с) раз на пачку; вона видна в
// даних як розрив elapsed_s, а не як тихо зіпсовані числа.
//
// Режим ALWAYS (радіо тримається піднятим) існує заради фази 0: порівняти
// σ у двох режимах і дізнатись, чи взагалі потрібен BURST на наших платах.

#ifdef AGRO_WIFI
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#ifndef WIFI_SSID
#error "AGRO_WIFI без -DWIFI_SSID: облікові дані передаються прапорцем, у репозиторії їм не місце"
#endif
#ifndef WIFI_PASS
#error "AGRO_WIFI без -DWIFI_PASS"
#endif
#ifndef INGEST_URL
#error "AGRO_WIFI без -DINGEST_URL"
#endif
#ifndef INGEST_TOKEN
#define INGEST_TOKEN ""
#endif

// Скільки замірів накопичуємо перед відправкою. 30 при періоді 1 с = пачка
// раз на 30 секунд.
#ifndef BATCH_SIZE
#define BATCH_SIZE 30
#endif

// Скільки пачок тримати, якщо мережа впала. У теплиці зв'язок рватиметься, і
// кожен втрачений шматок — це діра в кривій. 4 пачки = 2 хвилини історії;
// більше не тримаємо, бо RAM потрібна ще й на JSON.
#ifndef QUEUE_BATCHES
#define QUEUE_BATCHES 4
#endif

#define WIFI_CONNECT_TIMEOUT_MS 12000
#define HTTP_TIMEOUT_MS 8000

struct Reading {
  float elapsed;
  float waterMl;
  int raw[4];
  int mv[4];
  float airT;
  float airH;
  float vbat;
  int batPct;
  int batTrend;
  char event[24];
};

Reading batch[BATCH_SIZE];
int batchLen = 0;

// Черга — це просто готові JSON-тіла, які не вдалось віддати. Тримати їх
// рядками простіше, ніж переливати структури: пачка вже сформована, і
// повторна відправка не має її перебудовувати.
String pending[QUEUE_BATCHES];
int pendingLen = 0;

uint32_t batchSeq = 0;
String deviceId;

void wifiRadioOff() {
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
}

bool wifiRadioUp() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long until = millis() + WIFI_CONNECT_TIMEOUT_MS;
  while (WiFi.status() != WL_CONNECTED && (long)(millis() - until) < 0) {
    delay(100);
  }
  return WiFi.status() == WL_CONNECTED;
}

String jsonEscape(const char *s) {
  String out;
  for (const char *p = s; *p; p++) {
    if (*p == '"' || *p == '\\') {
      out += '\\';
    }
    out += *p;
  }
  return out;
}

// Формат навмисно повторює колонки CSV: дані з двох трактів мають бути
// зіставні поле в поле, інакше фаза 4 (звірка USB проти хмари) втрачає сенс.
String buildBatchJson() {
  String body;
  body.reserve(256 + (size_t)batchLen * 160);

  body += "{\"device\":\"" + deviceId + "\"";
  body += ",\"seq\":" + String(batchSeq);
  body += ",\"labels\":[";
  for (int i = 0; i < SOIL_COUNT; i++) {
    if (i) body += ",";
    body += "\"" + String(SOIL_LABELS[i]) + "\"";
  }
  body += "],\"samples\":[";

  for (int i = 0; i < batchLen; i++) {
    const Reading &r = batch[i];
    if (i) body += ",";
    body += "{\"t\":" + String(r.elapsed, 1);
    body += ",\"water_ml\":" + String(r.waterMl, 1);
    if (r.event[0]) {
      body += ",\"event\":\"" + jsonEscape(r.event) + "\"";
    }
    body += ",\"raw\":[";
    for (int k = 0; k < SOIL_COUNT; k++) { if (k) body += ","; body += String(r.raw[k]); }
    body += "],\"mv\":[";
    for (int k = 0; k < SOIL_COUNT; k++) { if (k) body += ","; body += String(r.mv[k]); }
    body += "]";
    // Порожньо, а не нуль — те саме правило, що в CSV: відсутній сенсор це
    // "не знаємо", і вигаданий нуль поїхав би в базу як справжні 0 °C.
    if (!isnan(r.airT)) body += ",\"air_t\":" + String(r.airT, 2);
    if (!isnan(r.airH)) body += ",\"air_h\":" + String(r.airH, 2);
    if (!isnan(r.vbat)) body += ",\"vbat\":" + String(r.vbat, 2);
    if (r.batPct >= 0) {
      body += ",\"bat_pct\":" + String(r.batPct);
      body += ",\"bat_trend\":" + String(r.batTrend);
    }
    body += "}";
  }

  body += "]}";
  return body;
}

bool postBody(const String &body) {
  // Клієнт обирається за схемою URL: https:// для хмари, http:// для
  // приймача в локальній мережі (фаза 0 і звірка фази 4 — tools/ingest_sink.py).
  // WiFiClientSecure на http-адресі мовчки провалить TLS-рукостискання, і
  // виглядало б це як «сервер не відповідає».
  static const bool secure = strncmp(INGEST_URL, "https://", 8) == 0;
  WiFiClient plain;
  WiFiClientSecure tls;
  // Без перевірки сертифіката: тримати кореневі CA у прошивці й оновлювати їх
  // при кожній ротації — окрема морока, а тут телеметрія калібрування, не
  // секрети. Токен у заголовку захищає endpoint від чужих записів.
  tls.setInsecure();
  WiFiClient &client = secure ? static_cast<WiFiClient &>(tls) : plain;

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(client, INGEST_URL)) {
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  if (strlen(INGEST_TOKEN) > 0) {
    http.addHeader("Authorization", "Bearer " INGEST_TOKEN);
  }

  int code = http.POST(body);
  http.end();

  Serial.printf("# POST %d, %u Б\n", code, (unsigned)body.length());
  return code >= 200 && code < 300;
}

void queuePush(const String &body) {
  if (pendingLen < QUEUE_BATCHES) {
    pending[pendingLen++] = body;
    return;
  }
  // Черга повна — викидаємо НАЙСТАРІШУ. Свіжі дані цінніші: калібрування
  // читається по кінцю кривої, а не по її початку.
  Serial.println("# черга переповнена, найстаріша пачка втрачена");
  for (int i = 1; i < QUEUE_BATCHES; i++) {
    pending[i - 1] = pending[i];
  }
  pending[QUEUE_BATCHES - 1] = body;
}

void flushBatch() {
  if (batchLen == 0) {
    return;
  }
  String body = buildBatchJson();
  batchSeq++;
  batchLen = 0;

  if (!wifiRadioUp()) {
    Serial.println("# Wi-Fi не піднявся — пачка в чергу");
    queuePush(body);
    return;
  }

  // Спершу борг, потім свіже: інакше при нестабільній мережі черга ніколи не
  // розсмокчеться.
  int sent = 0;
  while (sent < pendingLen && postBody(pending[sent])) {
    sent++;
  }
  if (sent > 0) {
    for (int i = sent; i < pendingLen; i++) {
      pending[i - sent] = pending[i];
    }
    pendingLen -= sent;
  }

  if (!postBody(body)) {
    queuePush(body);
  }

#ifndef AGRO_WIFI_ALWAYS
  wifiRadioOff();
#endif
}

void collectSample(float elapsed, float waterMl, const String &event) {
  if (batchLen >= BATCH_SIZE) {
    return;
  }
  Reading &r = batch[batchLen];
  r.elapsed = elapsed;
  r.waterMl = waterMl;
  for (int i = 0; i < SOIL_COUNT; i++) {
    r.raw[i] = lastRaw[i];
    r.mv[i] = lastMv[i];
  }
  r.airT = lastAirT;
  r.airH = lastAirH;
  r.vbat = lastVbat;
  r.batPct = lastBatPct;
  r.batTrend = batCharging;
  strncpy(r.event, event.c_str(), sizeof(r.event) - 1);
  r.event[sizeof(r.event) - 1] = 0;
  batchLen++;

  if (batchLen >= BATCH_SIZE) {
    flushBatch();
  }
}

void wifiSetup() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[18];
  snprintf(buf, sizeof(buf), "%02x%02x%02x%02x%02x%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  deviceId = String(buf);

  Serial.printf("# Wi-Fi: пристрій %s, пачка %d, режим %s\n",
                deviceId.c_str(), BATCH_SIZE,
#ifdef AGRO_WIFI_ALWAYS
                "ALWAYS (радіо не вимикається — тільки для фази 0)"
#else
                "BURST (радіо вимкнене під час заміру)"
#endif
  );
  if (wifiRadioUp()) {
    Serial.print("# Wi-Fi підключено, IP ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("# Wi-Fi недоступний — пишу тільки в USB, пачки в чергу");
  }
#ifndef AGRO_WIFI_ALWAYS
  wifiRadioOff();
#endif
}
#endif  // AGRO_WIFI

void powerOnVext() {
  pinMode(VEXT_CTRL, OUTPUT);
  digitalWrite(VEXT_CTRL, LOW);  // LOW = живлення подано
  vePowered = true;
  delay(100);
}

void powerOffVext() {
  digitalWrite(VEXT_CTRL, HIGH);
  vePowered = false;
}

void initDisplay() {
  powerOnVext();
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(20);
  digitalWrite(OLED_RST, HIGH);

  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
}

// Кома в тексті події зламала б CSV на рівному місці — міняємо її на ';'
// тут, а не сподіваємось, що користувач не набере кому в нотатці.
String csvSafe(String text) {
  text.replace(',', ';');
  text.replace('\n', ' ');
  return text;
}

void printHelp() {
  Serial.println("# commands (Enter after each):");
  Serial.println("#   50        - долив 50 мл (можна просто число)");
  Serial.println("#   w 50      - те саме, явно");
  Serial.println("#   n текст   - нотатка в колонку event");
  Serial.println("#   d         - мітка dry_ref (сенсор у сухому/повітрі)");
  Serial.println("#   s         - мітка wet_ref (сенсор у воді)");
  Serial.println("#   c 30 60   - крива прогріву: вікно 30 с після 60 с без живлення");
  Serial.println("#   x / o     - зняти / подати живлення сенсорів (Ve)");
#ifdef AGRO_WIFI
  Serial.println("#   f         - віддати недобрану пачку в хмару негайно");
#endif
  Serial.println("#   i         - скан шини I2C: хто відповідає");
  Serial.println("#   b         - сире значення дільника батареї");
  Serial.println("#   l         - які лінії SHT31 живі");
  Serial.println("#   h         - перевидати заголовок CSV");
  Serial.println("#   r         - новий прогін: обнулити час і воду");
  Serial.println("#   p 500     - період заміру, мс");
  Serial.println("#   ?         - ця довідка");
}

void printHeader() {
  Serial.println("# agro soil calibration bench");

  Serial.print("# sensors:");
  for (int i = 0; i < SOIL_COUNT; i++) {
    Serial.printf(" %s=GPIO%d", SOIL_LABELS[i], SOIL_PINS[i]);
  }
  Serial.println();

  Serial.printf("# oversample=%d period_ms=%lu\n", SOIL_OVERSAMPLE, samplePeriodMs);

  String header = "elapsed_s,water_ml,event";
  for (int i = 0; i < SOIL_COUNT; i++) {
    header += "," + String(SOIL_LABELS[i]) + "_raw";
    header += "," + String(SOIL_LABELS[i]) + "_mv";
  }
  // vbat/bat_pct — В КІНЕЦЬ, а не всередину: інструменти читають сенсорні
  // колонки за індексом, і вставка посеред рядка тихо зсунула б їх усі.
  header += ",air_t,air_h,vbat,bat_pct,bat_trend";
  Serial.println(header);
}

void readSoil(int index) {
  int pin = SOIL_PINS[index];

  // Перший замір після перемикання мультиплексора регулярно виходить кривим —
  // читаємо й викидаємо. Той самий обхід, що в бойовій прошивці.
  analogRead(pin);

  uint32_t rawSum = 0;
  uint32_t mvSum = 0;
  for (int i = 0; i < SOIL_OVERSAMPLE; i++) {
    rawSum += analogRead(pin);
    mvSum += analogReadMilliVolts(pin);
  }

  lastRaw[index] = rawSum / SOIL_OVERSAMPLE;
  lastMv[index] = mvSum / SOIL_OVERSAMPLE;
}

// Раз на 10 секунд пробуємо підняти SHT31, якщо його ще нема. Частіше нема
// сенсу: begin() тримає шину на час спроби, а сенсор або є, або нема.
#define AIR_RETRY_MS 10000

// Розчистити зависла шину I2C.
//
// Раб може утримувати SDA притиснутою до землі, якщо обмін урвався посеред
// байта — від завади, просадки живлення або смикнутого дроту. Шина стає
// мертвою, і begin() на ній провалюється так само, як читання: майстер бачить
// зайняту лінію й не може навіть видати START.
//
// Лікується не скиданням прапорців, а фізично: дев'ять тактів на SCL. Раб
// дотискає свій байт, відпускає SDA, після чого майстер видає STOP і шина
// вільна. Дев'ять — бо байт це вісім бітів плюс такт підтвердження.
//
// SHT31 у нас на постійних 3V3 і живлення йому не зняти, тож це ЄДИНИЙ спосіб
// повернути його без фізичного від'єднання.
void recoverI2C() {
  airWire.end();

  pinMode(AIR_SCL, OUTPUT);
  pinMode(AIR_SDA, INPUT_PULLUP);
  for (int i = 0; i < 9; i++) {
    digitalWrite(AIR_SCL, HIGH);
    delayMicroseconds(5);
    digitalWrite(AIR_SCL, LOW);
    delayMicroseconds(5);
  }

  // STOP: SDA відпускається у високий при високому SCL.
  pinMode(AIR_SDA, OUTPUT);
  digitalWrite(AIR_SDA, LOW);
  digitalWrite(AIR_SCL, HIGH);
  delayMicroseconds(5);
  digitalWrite(AIR_SDA, HIGH);
  delayMicroseconds(5);

  pinMode(AIR_SDA, INPUT_PULLUP);
  pinMode(AIR_SCL, INPUT_PULLUP);
  airWire.begin(AIR_SDA, AIR_SCL);
}

void tryFindAir() {
  if (airPresent || (long)(millis() - airRetryAt) < 0) {
    return;
  }
  airRetryAt = millis() + AIR_RETRY_MS;

  // Спершу розчищаємо шину: якщо сенсор тримає SDA, begin() без цього не
  // має шансів.
  recoverI2C();

  if (sht31.begin(0x44)) {
    airPresent = true;
    Serial.println("# SHT31 знайдено — колонки air_* заповнюються");
  }
}

void sample() {
  for (int i = 0; i < SOIL_COUNT; i++) {
    readSoil(i);
  }

  tryFindAir();
  readBattery();

  if (airPresent) {
    lastAirT = sht31.readTemperature();
    lastAirH = sht31.readHumidity();

    // Кілька NAN поспіль — сенсор зник із шини. Скидаємо прапорець, і
    // tryFindAir() почне пробувати begin() знову: щойно контакт повернеться,
    // колонки заповняться самі, без перезавантаження.
    if (isnan(lastAirT) || isnan(lastAirH)) {
      if (++airFailStreak >= AIR_FAIL_LIMIT) {
        airPresent = false;
        airFailStreak = 0;
        airRetryAt = millis();
        Serial.println("# SHT31 замовк — шукаю знову кожні 10 с");
      }
    } else {
      airFailStreak = 0;
    }
  } else {
    // Порожньо, а не нуль: відсутній сенсор це "не знаємо", і вигаданий нуль
    // поїхав би в CSV як справжні 0 °C.
    lastAirT = NAN;
    lastAirH = NAN;
  }

  String line = String((millis() - runStartMs) / 1000.0, 1);
  line += "," + String(waterMl, 1);
  line += "," + pendingEvent;

  for (int i = 0; i < SOIL_COUNT; i++) {
    line += "," + String(lastRaw[i]);
    line += "," + String(lastMv[i]);
  }

  // Порожньо, а не нуль: ємнісні сенсори дрейфують з температурою, тож
  // колонка air_t тут не прикраса — але вигаданий нуль зіпсував би її
  // сильніше, ніж пропуск.
  line += "," + (isnan(lastAirT) ? String("") : String(lastAirT, 2));
  line += "," + (isnan(lastAirH) ? String("") : String(lastAirH, 2));
  line += "," + (isnan(lastVbat) ? String("") : String(lastVbat, 2));
  line += "," + (lastBatPct < 0 ? String("") : String(lastBatPct));
  line += "," + (lastBatPct < 0 ? String("") : String(batCharging));

  Serial.println(line);

#ifdef AGRO_WIFI
  // Той самий замір іде в обидва тракти одночасно. Це і є основа фази 4:
  // розбіжність між USB і хмарою має бути нульовою, бо джерело одне.
  collectSample((millis() - runStartMs) / 1000.0, waterMl, pendingEvent);
#endif

  pendingEvent = "";
}

// Класичний індикатор у правому верхньому куті: корпус, носик, три поділки.
//
// Три, а не поступова смужка: на монохромному екрані 128x64 поділка — це 4
// пікселі, і плавне заповнення читалось би гірше за дискретне. Три стани
// (повна / половина / остання) — це рівно те, що потрібно вирішити в полі:
// працювати далі, готувати заміну, зупинятись.
//
// Нижче 20% корпус лишається порожнім — це і є попередження, помітне здалеку
// краще за будь-який текст.
void drawBattery(int pct) {
  const int w = 22, h = 10;
  const int x = 128 - w - 3;   // 3 пікселі під носик справа
  const int y = 1;

  display.drawRect(x, y, w, h);                 // корпус
  display.fillRect(x + w, y + 3, 3, h - 6);     // носик

  // Три поділки по 5 пікселів із проміжком.
  int bars = pct > 66 ? 3 : (pct > 33 ? 2 : (pct > 10 ? 1 : 0));
  for (int i = 0; i < bars; i++) {
    display.fillRect(x + 2 + i * 6, y + 2, 5, h - 4);
  }

  // Заряджання — риска впоперек корпусу. Блискавку в 10 пікселів висоти не
  // намалюєш розбірливо, а риска однозначна й видна здалеку.
  if (batCharging > 0) {
    display.drawLine(x + 3, y + h - 2, x + w - 3, y + 2);
  }

  // Відсотки лівіше корпусу: індикатор каже "приблизно", число — точно.
  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  display.drawString(x - 3, 0, String(pct) + "%");
  display.setTextAlignment(TEXT_ALIGN_LEFT);
}

void showOled() {
  if (!vePowered) {
    return;
  }
  display.clear();

  // Води тут нема навмисно: у польовому режимі доливи позначаються з телефона,
  // плата про них не знає, і рядок вічно показував би 0 мл. Замість нього —
  // стан мережі, який у полі якраз і треба бачити: чи доїжджають дані.
  String top;
#ifdef AGRO_WIFI
  top = (WiFi.status() == WL_CONNECTED) ? "WiFi OK" : "WiFi --";
  if (pendingLen > 0) {
    top += " q" + String(pendingLen);   // скільки пачок чекає в черзі
  }
#else
  top = "USB";
#endif
  if (!isnan(lastAirT)) {
    top += "  " + String(lastAirT, 1) + "C";
  }
  display.drawString(0, 0, top);

  // Заряд — окремо, у правому верхньому куті. У полі екран дивляться саме
  // щоб дізнатись, чи доживе прогін до кінця, тож це число не має губитись
  // серед решти рядка.
  //
  // Нічого не малюємо на USB: там lastBatPct = -1, і "0%" читалось би як
  // "розряджено вщент" замість "ми не на батареї".
  if (lastBatPct >= 0) {
    drawBattery(lastBatPct);
  }

  for (int i = 0; i < SOIL_COUNT; i++) {
    display.drawString(0, 12 + i * 13,
                       String(SOIL_LABELS[i]) + "  " + String(lastRaw[i]) +
                           "   " + String(lastMv[i]) + "mV");
  }

  display.display();
}

// Знімає криву виходу сенсора після подачі живлення: живлення геть -> пауза ->
// подали -> швидкі заміри від t=0. Відповідає на "скільки секунд треба, щоб
// сенсор показав правду" — те саме питання, що lowpower_probe вирішує для
// батарейного вузла, але тут на столі й з чотирма сенсорами одразу.
//
// Працює лише якщо VCC сенсорів на `Ve` (J2 pin 3/4). На постійних `3V3`
// знімати нема чого, і крива вийде рівною лінією — це теж читабельний
// результат, просто означає він "перевір, куди підключений VCC".
void probePowerCycle(unsigned long seconds, unsigned long offMs) {
  Serial.println("# --- probe: power cycle ---");
  Serial.printf("# off_ms=%lu period_ms=%d window_s=%lu\n", offMs,
                PROBE_PERIOD_MS, seconds);
  // Заголовок повторюється тут навмисно: блок probe має читатись сам по собі,
  // не покладаючись на те, що хтось бачив старт плати.
  printHeader();

  powerOffVext();
  delay(offMs);

  // t=0 — саме момент подачі, а не момент першого заміру: інакше перші
  // десятки мілісекунд (найцікавіші) поїхали б у зсув.
  runStartMs = millis();
  digitalWrite(VEXT_CTRL, LOW);
  vePowered = true;

  pendingEvent = "power_on";
  unsigned long until = runStartMs + seconds * 1000UL;
  while ((long)(millis() - until) < 0) {
    sample();
    delay(PROBE_PERIOD_MS);
  }

  Serial.println("# --- probe: done ---");

  // OLED сидів на тій самій шині й пережив зняття живлення — без повторної
  // ініціалізації на ньому лишиться сміття.
  initDisplay();
  showOled();

  // Годинник прогону пішов з нуля разом з probe — далі рахуємо від нього.
  nextSampleAt = millis() + samplePeriodMs;
}

// Подія має потрапити в дані тим самим замірам, а не чекати до кінця періоду:
// між "вилив" і "наступний тік" встигає початися вбирання води.
void markEvent(const String &event) {
  pendingEvent = csvSafe(event);
  sample();
  showOled();
  nextSampleAt = millis() + samplePeriodMs;
}

void addWater(float ml) {
  if (!(ml > 0)) {
    Serial.println("# ignored: треба додатне число мілілітрів");
    return;
  }
  waterMl += ml;
  markEvent("water+" + String(ml, 1));
}

void resetRun() {
  runStartMs = millis();
  nextSampleAt = millis();
  waterMl = 0;
  pendingEvent = "";
  Serial.println("# --- run reset ---");
  printHeader();
}

void handleCommand(const String &raw) {
  String line = raw;
  line.trim();
  if (line.length() == 0) {
    return;
  }

  char first = line.charAt(0);

  // Найчастіша дія за весь прогін — "я щойно вилив N мл", тож вона має
  // набиратись одним числом, без префікса.
  if (isDigit(first) || first == '.' || first == '+') {
    addWater(line.toFloat());
    return;
  }

  String rest = line.substring(1);
  rest.trim();

  switch (tolower(first)) {
    case 'w':
      addWater(rest.toFloat());
      break;
    case 'n':
      markEvent("note:" + rest);
      break;
    case 'd':
      markEvent("dry_ref");
      break;
    case 's':
      markEvent("wet_ref");
      break;
    case 'x':
      // Живлення геть і лишити знятим — щоб сенсор можна було безпечно
      // переставити руками в будь-якому порядку дротів. OLED сидить на тій
      // самій шині, тому темний екран — це і є індикатор "живлення нема".
      powerOffVext();
      Serial.println("# Ve OFF — можна міняти сенсор");
      break;
    case 'o':
      powerOnVext();
      initDisplay();
      Serial.println("# Ve ON");
      break;
#ifdef AGRO_WIFI
    case 'f':
      // Віддати недобрану пачку негайно. Потрібно наприкінці прогону: інакше
      // останні заміри висять у RAM і зникають разом із живленням.
      Serial.println("# примусова відправка");
      flushBatch();
      break;
#endif
    case 'l': {
      // Який саме дріт SHT31 відпав. ('w' зайнято доливом води.)
      //
      // На платі сенсора стоять підтягуючі резистори від SDA і SCL до його ж
      // VIN. Це й використовуємо: прижимаємо лінію до нуля, відпускаємо і
      // дивимось, чи підніметься вона сама. Підніметься — підтяжка жива,
      // отже і дріт, і живлення сенсора на місці.
      //
      // Внутрішні підтяжки ESP32 при цьому ВИМКНЕНІ, інакше лінія піднімалась
      // би завжди й тест нічого не розрізняв.
      airWire.end();
      bool up[2];
      const int pins[2] = {AIR_SDA, AIR_SCL};
      for (int k = 0; k < 2; k++) {
        pinMode(pins[k], OUTPUT);
        digitalWrite(pins[k], LOW);
        delayMicroseconds(50);
        pinMode(pins[k], INPUT);      // саме INPUT, без PULLUP
        delayMicroseconds(50);
        up[k] = digitalRead(pins[k]);
      }
      airWire.begin(AIR_SDA, AIR_SCL);

      Serial.printf("# SDA(GPIO%d): %s   SCL(GPIO%d): %s\n",
                    AIR_SDA, up[0] ? "піднялась" : "лежить",
                    AIR_SCL, up[1] ? "піднялась" : "лежить");
      if (up[0] && up[1]) {
        Serial.println("# обидві лінії й VIN на місці -> перевіряй ЗЕМЛЮ");
      } else if (!up[0] && !up[1]) {
        Serial.println("# жодна не піднялась -> нема VIN: живлення сенсора не доходить");
      } else {
        Serial.printf("# відпав %s\n", up[0] ? "SCL" : "SDA");
      }
      break;
    }
    case 'b': {
      // Сире значення з дільника батареї, без порогу 2,5 В.
      //
      // У штатному виводі все нижче порогу подається як "нема батареї" —
      // інакше плата на USB показувала б 0%, тобто "розряджено вщент".
      // Але при діагностиці треба бачити саме сире число: воно розрізняє
      // "нічого не підключено" (близько нуля) від "комірка є, але сіла"
      // (пара вольтів).
      // Полярність ADC_Ctrl на Heltec V3 залежить від ревізії плати: на одних
      // дільник вмикається LOW, на інших HIGH. Тому пробуємо обидві, а ще —
      // пін відпущений у високий імпеданс, як контроль.
      //
      // Рівно 0 мВ в усіх трьох станах означає, що справа не в полярності:
      // на вході ADC не було б навіть наводки.
      analogSetPinAttenuation(VBAT_ADC, ADC_11db);
      uint32_t probe[3];
      const char *how[3] = {"CTRL=LOW", "CTRL=HIGH", "CTRL відпущено"};
      for (int mode = 0; mode < 3; mode++) {
        if (mode == 2) {
          pinMode(VBAT_CTRL, INPUT);
        } else {
          pinMode(VBAT_CTRL, OUTPUT);
          digitalWrite(VBAT_CTRL, mode == 0 ? LOW : HIGH);
        }
        delay(20);
        uint32_t acc = 0;
        for (int k = 0; k < 16; k++) acc += analogReadMilliVolts(VBAT_ADC);
        probe[mode] = acc / 16;
        Serial.printf("#   %-16s -> %4lu мВ на піні = %.2f В\n",
                      how[mode], (unsigned long)probe[mode],
                      probe[mode] * VBAT_DIVIDER / 1000.0f);
      }
      pinMode(VBAT_CTRL, OUTPUT);
      digitalWrite(VBAT_CTRL, HIGH);

      uint32_t mv = probe[0] > probe[1] ? probe[0] : probe[1];
      float volts = (mv * VBAT_DIVIDER) / 1000.0f;
      Serial.printf("# VBAT: на піні %lu мВ -> %.2f В (дільник %.1f)\n",
                    (unsigned long)mv, volts, VBAT_DIVIDER);
      Serial.println(volts < 0.3f  ? "#   ~0 — на роз'ємі нічого немає"
                     : volts < 2.5f ? "#   є напруга, але дуже низька: комірка сіла або поганий контакт"
                                    : "#   батарея на місці");
      if (batCharging > 0)      Serial.println("#   напруга росте — ЗАРЯДЖАЄТЬСЯ");
      else if (batCharging < 0) Serial.println("#   напруга падає — розряджається");
      break;
    }
    case 'i': {
      // Скан шини I2C: пройти всі адреси й подивитись, хто відповідає.
      //
      // Це припиняє гадання. Пошук наосліп («а може SDA і SCL місцями?»)
      // коштував нам чотирьох спроб; скан дає відповідь за секунду:
      //   0x44 відповідає  -> сенсор на шині, справа в читанні
      //   нікого           -> обрив, живлення або переплутані лінії
      recoverI2C();
      Serial.println("# скан I2C:");
      int found = 0;
      for (uint8_t addr = 1; addr < 127; addr++) {
        airWire.beginTransmission(addr);
        if (airWire.endTransmission() == 0) {
          Serial.printf("#   0x%02X відповідає%s\n", addr,
                        addr == 0x44 ? "  <- SHT31" : "");
          found++;
        }
      }
      if (found == 0) {
        Serial.println("#   нікого. Перевір живлення, землю й лінії SDA/SCL");
      }
      break;
    }
    case 'h':
      // Скрипт, який під'єднався до вже запущеної плати, заголовка не бачив —
      // а без нього не знає, який стовпець це який сенсор. `r` для цього не
      // годиться: він обнулив би воду посеред прогону.
      printHeader();
      break;
    case 'r':
      resetRun();
      break;
    case 'c': {
      // "c <вікно_с> [пауза_с]" — друге число керує глибиною розряду
      // детектора, і саме воно вирішує, чи можна переносити результат на
      // вузол зі сном.
      int space = rest.indexOf(' ');
      long seconds = (space < 0 ? rest : rest.substring(0, space)).toInt();
      long offS = space < 0 ? 0 : rest.substring(space + 1).toInt();

      probePowerCycle(seconds > 0 ? (unsigned long)seconds : PROBE_DEFAULT_S,
                      offS > 0 ? (unsigned long)offS * 1000UL : PROBE_OFF_MS);
      break;
    }
    case 'p': {
      long ms = rest.toInt();
      if (ms < 100) {
        Serial.println("# ignored: період < 100 мс");
        break;
      }
      samplePeriodMs = ms;
      Serial.printf("# period_ms=%lu\n", samplePeriodMs);
      break;
    }
    case '?':
      printHelp();
      break;
    default:
      Serial.println("# unknown command, '?' для довідки");
  }
}

void readSerialCommands() {
  while (Serial.available()) {
    char ch = Serial.read();

    if (ch == '\n' || ch == '\r') {
      if (cmdBuf.length() > 0) {
        handleCommand(cmdBuf);
        cmdBuf = "";
      }
    } else if (cmdBuf.length() < CMD_MAX_LEN) {
      cmdBuf += ch;
    }
  }
}

// Чому плата перезавантажилась минулого разу. Причина лежить в апаратному
// регістрі й переживає саме скидання, тож це єдиний надійний спосіб
// відрізнити просадку живлення від зависання коду.
//
// BROWNOUT при підключеній батареї майже завжди означає одне: USB не тягне
// заряджання комірки разом з імпульсами Wi-Fi (500 мА заряду + 300 мА
// передачі + 80 мА плати перевищують те, що дає порт).
void reportResetReason() {
  const char *why;
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  why = "подано живлення"; break;
    case ESP_RST_SW:       why = "програмне (заливка)"; break;
    case ESP_RST_BROWNOUT: why = "ПРОСАДКА ЖИВЛЕННЯ — не вистачає струму"; break;
    case ESP_RST_PANIC:    why = "ПАНІКА — збій у коді"; break;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      why = "WATCHDOG — код завис"; break;
    case ESP_RST_DEEPSLEEP: why = "вихід зі сну"; break;
    default:               why = "інше"; break;
  }
  Serial.printf("# причина старту: %s\n", why);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  reportResetReason();
  initDisplay();
  display.clear();
  display.drawString(0, 0, "SOIL CALIBRATION");
  display.drawString(0, 16, "warmup...");
  display.display();

  analogReadResolution(12);
  for (int i = 0; i < SOIL_COUNT; i++) {
    // 11 dB розтягує вхід на ~0..3,1 В: ємнісний сенсор на 3,3 В віддає
    // приблизно 1,2..2,6 В, на меншому атенюаторі верх діапазону зрізало б.
    analogSetPinAttenuation(SOIL_PINS[i], ADC_11db);
    lastRaw[i] = 0;
    lastMv[i] = 0;
  }

  pinMode(AIR_SDA, INPUT_PULLUP);
  pinMode(AIR_SCL, INPUT_PULLUP);
  airWire.begin(AIR_SDA, AIR_SCL);
  airPresent = sht31.begin(0x44);
  if (!airPresent) {
    Serial.println("# SHT31 не знайдено — пробую далі кожні 10 с");
  }

  delay(SOIL_WARMUP_MS);

#ifdef AGRO_WIFI
  wifiSetup();
#endif

  printHelp();
  resetRun();
}

void loop() {
  readSerialCommands();

  if ((long)(millis() - nextSampleAt) >= 0) {
    sample();
    showOled();

    nextSampleAt += samplePeriodMs;
    // Якщо чомусь відстали більше ніж на період (довга команда, гальма I2C) —
    // не наздоганяємо пачкою замірів, а починаємо відлік від зараз.
    if ((long)(millis() - nextSampleAt) > 0) {
      nextSampleAt = millis() + samplePeriodMs;
    }
  }
}
