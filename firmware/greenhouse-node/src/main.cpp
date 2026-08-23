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


int soilRawToPercent(int humSoil) {
  // Грубе перетворення сирого ADC у % — без реального калібрування (буде пізніше).
  // Підбери fromLow/fromHigh під свої реальні сирі значення сухо/мокро.
  int soilPercent = map(humSoil, 3000, 1200, 0, 100);
  return constrain(soilPercent, 0, 100);
}

// uptime тут не прикраса: опорний вузол має працювати безперервно, і саме
// його неперервність робить його опорним. Якщо він мовчки перезавантажиться,
// лічильник впаде до нуля — і це буде видно в даних, а не здогадкою.
String buildTelemetry(int soilPercent, float airTemp, float airHum, float vbat) {
  return "{\"type\":\"measurement\",\"node_id\":" + String(NODE_ID) + ","
           "\"air_temperature\":" + String(airTemp, 1) + ","
           "\"air_humidity\":" + String(airHum, 1) + ","
           "\"soil_moisture\":" + String(soilPercent) + ","
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

  String msg = buildTelemetry(soilPercent, airTemp, airHum, vbat);
  int state = radio.transmit(msg);

  if (state == RADIOLIB_ERR_NONE) {
    LOG_LN("Sent! " + msg);
  } else {
    LOG("Send failed, code %d\n", state);
  }

  showTelemetry(soilPercent, airTemp, airHum);
  delay(1000);
}
