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

// Пінаут Heltec WiFi LoRa 32 V3 — той самий, що в решті прошивок.
#define OLED_SDA   17
#define OLED_SCL   18
#define OLED_RST   21
#define VEXT_CTRL  36

#define AIR_SDA    41
#define AIR_SCL    42

// Чотири сенсори замість трьох: GPIO2/3/4 — як у вузлі, плюс GPIO5. Усі
// чотири на ADC1 (на ESP32-S3 це GPIO1..GPIO10), і це не дрібниця: ADC2 на
// цій платі ділиться з Wi-Fi і при його вмиканні просто перестає читати.
#define SOIL_1  2
#define SOIL_2  3
#define SOIL_3  4
#define SOIL_4  5

#define SOIL_COUNT 4

const int SOIL_PINS[SOIL_COUNT] = {SOIL_1, SOIL_2, SOIL_3, SOIL_4};

// Мітки їдуть у заголовок CSV, тобто це імена стовпців у майбутній таблиці.
// Порядок має відповідати тому, як сенсори реально встромлені у відро —
// інакше вся різниця "v1.2 проти v2.0" перемішається. Міняти тут, коли
// міняється розкладка на столі.
const char *SOIL_LABELS[SOIL_COUNT] = {"v12a", "v12b", "v20a", "v20b"};

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

int lastRaw[SOIL_COUNT];
int lastMv[SOIL_COUNT];
float lastAirT = NAN;
float lastAirH = NAN;

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
  int raw[SOIL_COUNT];
  int mv[SOIL_COUNT];
  float airT;
  float airH;
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
  header += ",air_t,air_h";
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

void tryFindAir() {
  if (airPresent || (long)(millis() - airRetryAt) < 0) {
    return;
  }
  airRetryAt = millis() + AIR_RETRY_MS;

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

  if (airPresent) {
    lastAirT = sht31.readTemperature();
    lastAirH = sht31.readHumidity();
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

  Serial.println(line);

#ifdef AGRO_WIFI
  // Той самий замір іде в обидва тракти одночасно. Це і є основа фази 4:
  // розбіжність між USB і хмарою має бути нульовою, бо джерело одне.
  collectSample((millis() - runStartMs) / 1000.0, waterMl, pendingEvent);
#endif

  pendingEvent = "";
}

void showOled() {
  if (!vePowered) {
    return;
  }
  display.clear();

  String top = "H2O " + String(waterMl, 0) + " ml";
  if (!isnan(lastAirT)) {
    top += "   " + String(lastAirT, 1) + "C";
  }
  display.drawString(0, 0, top);

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

void setup() {
  Serial.begin(115200);
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
