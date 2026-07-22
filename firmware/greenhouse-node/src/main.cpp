// Тест бринг-апу: перевірити що SX1262 і OLED на Heltec WiFi LoRa 32 V3 живі.
// Ще не протокол з greenhouse_architecture.md — просто "радіо ініціалізувалось,
// плата не мертва". Частота 868 МГц (EU868), бо плата HF-варіант (863-928 МГц).

#include <Arduino.h>
#include <RadioLib.h>
#include <SSD1306Wire.h>

// Пінаут Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262)
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

SSD1306Wire display(0x3c, OLED_SDA, OLED_SCL);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);

void showStatus(const String &line1, const String &line2) {
  display.clear();
  display.drawString(0, 0, line1);
  display.drawString(0, 16, line2);
  display.display();
}

void setup() {
  Serial.begin(115200);

  // Vext живить OLED і периферію на Heltec V3 — треба увімкнути (LOW = on).
  pinMode(VEXT_CTRL, OUTPUT);
  digitalWrite(VEXT_CTRL, LOW);
  delay(100);

  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(20);
  digitalWrite(OLED_RST, HIGH);

  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  showStatus("Booting...", "");

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  int state = radio.begin(868.0);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("LoRa init OK (868 MHz)");
    showStatus("LoRa: OK", "868 MHz");
  } else {
    Serial.printf("LoRa init failed, code %d\n", state);
    showStatus("LoRa: FAIL", "code " + String(state));
  }

  display.drawString(0, 40, "Holub Pryvit!");
  display.display();
}

void loop() {
  delay(1000);
}
