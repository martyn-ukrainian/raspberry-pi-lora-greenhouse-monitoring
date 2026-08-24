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
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) {
    mv += analogReadMilliVolts(VBAT_ADC);
  }
  mv /= 8;

  digitalWrite(VBAT_CTRL, LOW);
  return (mv * VBAT_DIVIDER) / 1000.0f;
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


// Межі й форма — ЗАМІРЯНІ (docs/калібрування-ґрунту.md):
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
// SOIL_ASYMPTOTE трохи нижча за мокрий кінець навмисно: біля самої асимптоти
// логарифм росте необмежено, і шум почав би давати відсотки на рівному місці.
#define SOIL_DRY_RAW    2874
#define SOIL_WET_RAW    1050
#define SOIL_ASYMPTOTE  1020

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
           "\"vbat\":" + String(vbat, 2) + ","
           "\"uptime\":" + String(millis() / 1000) + "}";
}

void showTelemetry(int soilPercent, float airTemp, float airHum) {
  display.clear();
  display.drawString(0, 0, "AIR TEMP: " + String(airTemp, 1) + "°C");
  display.drawString(0, 14, "AIR HUM: " + String(airHum, 1) + "%");
  display.drawString(0, 28, "SOIL: " + String(soilPercent) + "%");
  display.drawString(0, 42, "BAT: " + String(lastVbat, 2) + "V");
  display.display();
}

void loop() {
  SoilReadings soil = readSoilSensors();
  int humSoil = medianOf3(soil.s1, soil.s2, soil.s3);
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
