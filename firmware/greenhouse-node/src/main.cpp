// Тест бринг-апу: перевірити що SX1262 і OLED на Heltec WiFi LoRa 32 V3 живі.
// Ще не протокол з greenhouse_architecture.md — просто "радіо ініціалізувалось,
// плата не мертва". Частота 868 МГц (EU868), бо плата HF-варіант (863-928 МГц).

#include <Arduino.h>
#include <RadioLib.h>
#include <SSD1306Wire.h>

// Пінаут Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262)
#define NODE_ID    0

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

SSD1306Wire display(0x3c, OLED_SDA, OLED_SCL);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);

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

void showStatus(const String &line1, const String &line2) {
  display.clear();
  display.drawString(0, 0, line1);
  display.drawString(0, 16, line2);
  display.display();
}

void setup() {
  Serial.begin(115200);

  initDisplay();
  showStatus("Loading...", "");

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  int state = radio.begin(868.0);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("LoRa init OK (868 MHz)");
    showStatus("LoRa: OK", "868 MHz");
  } else {
    Serial.printf("LoRa init failed, code %d\n", state);
    showStatus("LoRa: FAIL", "code " + String(state));
  }

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

// Поки нема SHT31 — фіксовані значення (буде замінено на реальні виміри).
#define AIR_TEMPERATURE 0
#define AIR_HUMIDITY    0

int soilRawToPercent(int humSoil) {
  // Грубе перетворення сирого ADC у % — без реального калібрування (буде пізніше).
  // Підбери fromLow/fromHigh під свої реальні сирі значення сухо/мокро.
  int soilPercent = map(humSoil, 3000, 1200, 0, 100);
  return constrain(soilPercent, 0, 100);
}

String buildTelemetry(int soilPercent) {
  return "{\"type\":\"measurement\",\"node_id\":" + String(NODE_ID) + ","
           "\"air_temperature\":" + String(AIR_TEMPERATURE) + ","
           "\"air_humidity\":" + String(AIR_HUMIDITY) + ","
           "\"soil_moisture\":" + String(soilPercent) + "}";
}

void showTelemetry(int soilPercent) {
  display.clear();
  display.drawString(0, 0, "AIR TEMP: " + String(AIR_TEMPERATURE) + "°C");
  display.drawString(0, 14, "AIR HUM: " + String(AIR_HUMIDITY) + "%");
  display.drawString(0, 28, "SOIL: " + String(soilPercent) + "%");
  display.display();
}

void loop() {
  SoilReadings soil = readSoilSensors();
  int humSoil = medianOf3(soil.s1, soil.s2, soil.s3);
  int soilPercent = soilRawToPercent(humSoil);

  String msg = buildTelemetry(soilPercent);
  int state = radio.transmit(msg);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("Sent! " + msg);
  } else {
    Serial.printf("Send failed, code %d\n", state);
  }

  showTelemetry(soilPercent);
  delay(1000);
}
