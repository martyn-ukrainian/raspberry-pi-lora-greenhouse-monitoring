// Тест бринг-апу: перевірити що SX1262 і OLED на Heltec WiFi LoRa 32 V3 живі.
// Ще не протокол з greenhouse_architecture.md — просто "радіо ініціалізувалось,
// плата не мертва". Частота 868 МГц (EU868), бо плата HF-варіант (863-928 МГц).

#include <Arduino.h>
#include <RadioLib.h>
#include <SSD1306Wire.h>
#include <Adafruit_SHT31.h>

// Пінаут Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262)
// Номер вузла в ефірі. Задається збіркою, а не правкою коду: на етапі 3 два
// вузли стоять поруч і шлють одночасно, тож однаковий NODE_ID зробив би їх
// нерозрізненними в базі — а вся суть досліду в тому, щоб порівняти саме їх.
//
//   PLATFORMIO_BUILD_FLAGS=-DNODE_ID=1 make deploy PROJECT=...
//
// Число має бути зареєстроване в server/config/nodes.yaml, інакше адаптер
// відсіє пакет як від невідомого вузла.
#ifndef NODE_ID
#define NODE_ID 0
#endif

#define LORA_NSS   8
#define LORA_SCK   9
#define LORA_MOSI  10
#define LORA_MISO  11
#define LORA_RST   12
#define LORA_BUSY  13
#define LORA_DIO1  14

#define OLED_SDA   17
#define OLED_SCL   18
#define OLED_RST   21
#define VEXT_CTRL  36

#define SOIL_1     2
#define SOIL_2     3
#define SOIL_3     4

// Скільки ґрунтових щупів РЕАЛЬНО підключено. Не косметика: доти код брав
// медіану всіх трьох входів беззастережно, а підключений у нас один.
//
// Медіана з трьох існує, щоб пережити один відпалий сенсор. Але коли двох
// сенсорів нема зовсім, вона віддає шум із порожніх входів — і робить це
// переконливо. Вузол 1 місяцями рапортував 14-15% вологості, тоді як його
// щуп на s1 показував 3306, тобто сухо. Перевірено водою на вузлі 2: у воді
// s1 падає 3230 -> 1002, а s2/s3 не ворушаться зовсім.
//
// Три щупи повернути можна прапорцем, не правкою коду:
//   PLATFORMIO_BUILD_FLAGS=-DSOIL_SENSOR_COUNT=3 make upload PROJECT=...
#ifndef SOIL_SENSOR_COUNT
#define SOIL_SENSOR_COUNT 1
#endif

#define AIR_SDA    41
#define AIR_SCL    42

// Вбудований дільник вимірювання батареї: GPIO37 вмикає його (LOW = on),
// GPIO1 читає. Той самий вузол, що в greenhouse-node-lowpower.
//
// Тут він потрібен не для алертів, а для етапу 3: опорний вузол і вузол зі
// сном стоять поруч і розряджають однакові комірки, тож порівняти криві
// розряду можна лише якщо ОБИДВА шлють vbat. Без цього поля в опорному
// порівнювати нема з чим.
#define VBAT_ADC   1
#define VBAT_CTRL  37
#define VBAT_DIVIDER 4.9f

SSD1306Wire display(0x3c, OLED_SDA, OLED_SCL);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);

TwoWire airWire = TwoWire(1);
Adafruit_SHT31 sht31 = Adafruit_SHT31(&airWire);

// Останній замір батареї — щоб showTelemetry() не робив власний,
// не вмикаючи дільник двічі за цикл.
float lastVbat = 0;

// Останнє СИРЕ значення ґрунту — те саме, що йде в soil_raw телеметрії.
// На екран іде саме воно, а не відсоток: відсоток залежить від калібрування,
// яке ще не завершене, тож на столі він вводить в оману. Сире число не
// залежить ні від чого й порівнюється між вузлами напряму.
int lastSoilRaw = 0;

void powerOnVext() {
  pinMode(VEXT_CTRL, OUTPUT);
  digitalWrite(VEXT_CTRL, LOW);
  delay(100);
}

void resetDisplay() {
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(20);
  digitalWrite(OLED_RST, HIGH);
}

void initDisplay() {
  // Vext живить OLED і периферію на Heltec V3 — треба увімкнути (LOW = on).
  powerOnVext();
  resetDisplay();
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
}

// ---------------------------------------------------------------------------
// Serial: той самий вимикач, що і в greenhouse-node-lowpower
// ---------------------------------------------------------------------------
// Ця прошивка стендова: вона живе на столі з підключеним USB, і друк — це
// половина її сенсу, тому -DAGRO_DEBUG_SERIAL стоїть у platformio.ini і
// друк за замовчуванням УВІМКНЕНО. Механізм тут заради єдиного стилю: якщо
// цю прошивку колись понесуть у теплицю, вимикач уже на місці, і не треба
// згадувати, що саме в ній Serial живий, а в сусідній ні.
//
// Префікс AGRO_ обов'язковий: у Adafruit BusIO є свій макрос DEBUG_SERIAL,
// голе ім'я ламає збірку бібліотеки.

#ifdef AGRO_DEBUG_SERIAL
#define LOG_BEGIN() Serial.begin(115200)
#define LOG(...)    Serial.printf(__VA_ARGS__)
#define LOG_LN(x)   Serial.println(x)
#else
// Аргументи не обчислюються — не класти в LOG() нічого, крім читання
// вже готових значень.
#define LOG_BEGIN() ((void)0)
#define LOG(...)    ((void)0)
#define LOG_LN(x)   ((void)0)
#endif

void showStatus(const String &line1, const String &line2) {
  display.clear();
  display.drawString(0, 0, line1);
  display.drawString(0, 16, line2);
  display.display();
}

void setup() {
  LOG_BEGIN();
  initDisplay();
  showStatus("Loading...", "");

  pinMode(AIR_SDA, INPUT_PULLUP);
  pinMode(AIR_SCL, INPUT_PULLUP);
  airWire.begin(AIR_SDA, AIR_SCL);

  if (!sht31.begin(0x44)) {
    LOG_LN("SHT31 not found");
  }

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  int state = radio.begin(868.0);

  if (state == RADIOLIB_ERR_NONE) {
    LOG_LN("LoRa init OK (868 MHz)");
    showStatus("LoRa: OK", "868 MHz");
  } else {
    LOG("LoRa init failed, code %d\n", state);
    showStatus("LoRa: FAIL", "code " + String(state));
  }

}


// Дільник вмикається тільки на час заміру: увімкнений постійно, він сам
// висаджує батарею.
float readBatteryVolts() {
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
  // ЧИ ЦЕ ВЗАГАЛІ БАТАРЕЯ.
  //
  // Поріг "нижче 2,5 В = батареї нема" не працює: на USB БЕЗ комірки дільник
  // читає шину зарядника, тобто цілком правдоподібні 3,7-4,1 В. Прошивка
  // рапортувала б неіснуючу батарею, і саме це заплутало нас 2026-08-23.
  //
  // Розрізняє їх СТАБІЛЬНІСТЬ, а не рівень: шина зарядника імпульсна й гуляє
  // на сотні мілівольт між замірами, а справжня комірка стоїть рівно —
  // заміряно в теплиці, 0,04 В за 50 хвилин.
  uint32_t lo = 4095, hi = 0;
  for (int i = 0; i < 8; i++) {
    uint32_t one = analogReadMilliVolts(VBAT_ADC);
    if (one < lo) lo = one;
    if (one > hi) hi = one;
    delay(3);
  }
  const bool unstable = (hi - lo) > 40;   // 40 мВ на дільнику = ~200 мВ на комірці

  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) {
    mv += analogReadMilliVolts(VBAT_ADC);
  }
  mv /= 8;

  digitalWrite(VBAT_CTRL, LOW);

  float volts = (mv * VBAT_DIVIDER) / 1000.0f;
  // NAN, а не нуль: "не знаємо" це не "розряджено вщент".
  return (unstable || volts < 2.5f) ? NAN : volts;
}

struct SoilReadings {
  int s1, s2, s3;
};

SoilReadings readSoilSensors() {
  SoilReadings r;
  r.s1 = analogRead(SOIL_1);
  r.s2 = analogRead(SOIL_2);
  r.s3 = analogRead(SOIL_3);
  return r;
}

// String textMessage(const SoilReadings &soil) {
//   String txt = "hello from gh_1, " + String(soil.s1) + " " + String(soil.s2) + " " + String(soil.s3);
//   return txt;
// }


int medianOf3(int a, int b, int c) {
  if (a > b) { int t = a; a = b; b = t; }
  if (b > c) { int t = b; b = c; c = t; }
  if (a > b) { int t = a; a = b; b = t; }
  return b;
}


// Межі й форма — ЗАМІРЯНІ (docs/дослідження/калібрування-ґрунту.md):
//
//   2874 raw — повітряно-сухий тепличний ґрунт, стіл 2026-08-22
//   1050 raw — грядка після звичайного поливу, теплиця 2026-08-23
//   ~1020 raw — асимптота: показник, у який упирається насичення
//
// ЧОМУ НЕ map(). Відгук сенсора експоненційний: у сухому ґрунті 1 мл води
// зсуває показник на 12 мВ, у мокрому — на 0,05 мВ. Тобто лінійна шкала по
// напрузі НЕ лінійна по воді: вона з'їдає всю мокру половину діапазону.
// Заміряно: від 1200 до 1050 raw у лінійній шкалі лише 8 відсотків, хоча це
// більша частина води.
//
// Логарифм повертає шкалі зміст: рівні кроки відсотка = рівні порції води.
// Практично це виглядає так, як і мало б: до мокрого відсоток біжить швидше,
// до сухого — повільніше.
//
// Коефіцієнт k з моделі mV = C + A*exp(-V/k) тут СКОРОЧУЄТЬСЯ — потрібна лише
// асимптота C. Це важливо, бо k залежить від об'єму ґрунту (заміряний на 1 кг
// у відрі й на грядку не переноситься), а C — властивість сенсора й ґрунту.
//
// SOIL_ASYMPTOTE = 1043 обрана НЕ навмання: це асимптота C з моделі, переведена
// в одиниці ADC. Саме при ній шкала стає лінійною по воді — половина влитої
// води дає рівно 50%. Зсунеш нижче (1020) — і половина води покаже 63%, тобто
// шкала знову почне щось приховувати.
//
// Ціна точності — шум біля мокрого краю: 2,2% проти 1,2% при 1020. Це чесна
// плата: у насиченому ґрунті сенсор справді не розрізняє воду, і шкала має це
// показувати, а не згладжувати.
#define SOIL_DRY_RAW    2874
#define SOIL_WET_RAW    1050
#define SOIL_ASYMPTOTE  1043

int soilRawToPercent(int humSoil) {
  // Нижче асимптоти логарифм не визначений — це вже поза шкалою.
  if (humSoil <= SOIL_ASYMPTOTE + 1) {
    return 100;
  }
  if (humSoil >= SOIL_DRY_RAW) {
    return 0;
  }

  const float span = (float)(SOIL_DRY_RAW - SOIL_ASYMPTOTE);
  const float full = logf((float)(SOIL_WET_RAW - SOIL_ASYMPTOTE) / span);
  const float here = logf((float)(humSoil - SOIL_ASYMPTOTE) / span);

  int soilPercent = (int)(100.0f * here / full);
  return constrain(soilPercent, 0, 100);
}

// uptime тут не прикраса: опорний вузол має працювати безперервно, і саме
// його неперервність робить його опорним. Якщо він мовчки перезавантажиться,
// лічильник впаде до нуля — і це буде видно в даних, а не здогадкою.
// soil_raw їде поруч із відсотком навмисно. Відсоток — ПОХІДНА, і його шкала
// ще не остаточна: робочий діапазон грядки невідомий, поки не побачимо цикл
// висихання. Сире значення від шкали не залежить, тож коли межі уточняться,
// історію можна перерахувати без перепрошивки вузлів у полі.
String buildTelemetry(int soilRaw, int soilPercent, float airTemp, float airHum,
                      float vbat) {
  return "{\"type\":\"measurement\",\"node_id\":" + String(NODE_ID) + ","
           "\"air_temperature\":" + String(airTemp, 1) + ","
           "\"air_humidity\":" + String(airHum, 1) + ","
           "\"soil_moisture\":" + String(soilPercent) + ","
           "\"soil_raw\":" + String(soilRaw) + ","
           + (isnan(vbat) ? String("") : "\"vbat\":" + String(vbat, 2) + ",")
           + 
           "\"uptime\":" + String(millis() / 1000) + "}";
}

void showTelemetry(int soilPercent, float airTemp, float airHum) {
  display.clear();
  display.drawString(0, 0, "AIR TEMP: " + String(airTemp, 1) + "°C");
  display.drawString(0, 14, "AIR HUM: " + String(airHum, 1) + "%");
  // Сире ADC замість відсотка: 0..4095, менше = вологіше.
  display.drawString(0, 28, "SOIL: " + String(lastSoilRaw) + " adc");
  display.drawString(0, 42, "BAT: " + String(lastVbat, 2) + "V");
  display.display();
}

void loop() {
  SoilReadings soil = readSoilSensors();
#if SOIL_SENSOR_COUNT == 3
  int humSoil = medianOf3(soil.s1, soil.s2, soil.s3);
#else
  int humSoil = soil.s1;
#endif
  lastSoilRaw = humSoil;

  // Три входи окремо, а не лише медіана. Медіана саме для того й потрібна, щоб
  // пережити один відпалий сенсор мовчки — але на столі це шкодить: не видно,
  // до якого піна щуп узагалі підключений і чи підключений хоч до одного.
  // Порожній вхід плаває під стелею АЦП, справжній щуп у ґрунті дає 1000-3000.
  // "using", а не "median": при SOIL_SENSOR_COUNT=1 медіани тут нема, і
  // старий підпис приховував би саме те, що ми щойно ловили.
  LOG("soil pins: s1=%d s2=%d s3=%d -> using %d\n",
      soil.s1, soil.s2, soil.s3, humSoil);
  int soilPercent = soilRawToPercent(humSoil);

  float airTemp = sht31.readTemperature();
  float airHum = sht31.readHumidity();

  if (isnan(airTemp) || isnan(airHum)) {
    LOG_LN("SHT31 read failed (NAN) — check wiring");
    airTemp = 0;
    airHum = 0;
  }


  float vbat = readBatteryVolts();
  lastVbat = vbat;

  String msg = buildTelemetry(humSoil, soilPercent, airTemp, airHum, vbat);
  int state = radio.transmit(msg);

  if (state == RADIOLIB_ERR_NONE) {
    LOG_LN("Sent! " + msg);
  } else {
    LOG("Send failed, code %d\n", state);
  }

  showTelemetry(soilPercent, airTemp, airHum);
  delay(1000);
}
