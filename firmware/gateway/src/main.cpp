#include <Arduino.h>
#include <RadioLib.h>
#include <SSD1306Wire.h>

// Пінаут Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262)
#define MAX_NODES 3

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

// Вузол шле раз/сек — 10 сек тиші вже вважаємо втратою зв'язку, а не просто
// пропущеним пакетом.
#define CONNECTION_TIMEOUT_MS 10000

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
  delay(100);
  digitalWrite(OLED_RST, HIGH);
}

void showStatus(const String &line1, const String &line2) {
  display.clear();
  display.drawString(0, 0, line1);
  display.drawString(0, 10, line2);
  display.display();
}

void initDisplay() {
  powerOnVext();
  resetDisplay();
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
}

// Блокуючий radio.receive() рахує свій timeout ще до того, як знає довжину
// пакету — для коротких повідомлень встигає, для довших (наш JSON) вже ні,
// приймання перерветься серед пакету. Неблокуючий режим (startReceive +
// переривання) цієї проблеми не має — просто чекає RxDone скільки треба.
volatile bool receivedFlag = false;

#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void onReceive() {
  receivedFlag = true;
}

int nodeIds[MAX_NODES] = {0, 1, 2};
unsigned long lastSeen[MAX_NODES] = {0, 0, 0};
int lastRssi[MAX_NODES] = {0, 0, 0};
float lastSnr[MAX_NODES] = {0, 0, 0};

int parseNodeId(const String &json) {
  int idx = json.indexOf("\"node_id\":");
  if (idx == -1) return -1;  // не знайшли — щось не так з форматом

  int start = idx + strlen("\"node_id\":");
  return json.substring(start).toInt();
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

  display.drawString(0, 40, "Hello");
  display.display();

  radio.setPacketReceivedAction(onReceive);
  radio.startReceive();
}

void reportReceived(const String &received, int rssi, float snr) {
  String withSignal = received.substring(0, received.length() - 1)
    + ",\"rssi\":" + String(rssi)
    + ",\"snr\":" + String(snr, 1)
    + "}";
  Serial.println(withSignal);
}

String getSecAgo(unsigned long seenAtMillis) {
  unsigned long secAgo = (millis() - seenAtMillis) / 1000;
  return String(secAgo) + "s ago";
}

// Груба класифікація "наскільки хороший" зв'язок — орієнтовно під LoRa
// 868 МГц (SX1262). Від'ємний SNR тут — нормальне явище, не проблема.
// Повертає 1-3 (скільки риски закрасити з трьох).
int signalTier(int rssi, float snr) {
  if (rssi > -90 && snr > 0) return 3;    // strong
  if (rssi > -110 && snr > -10) return 2; // medium
  return 1;                               // weak
}

// Три риски зростаючої висоти (як індикатор сигналу на телефоні), вирівняні
// по нижньому краю. tier=0 — всі риски лише контуром (сигналу нема).
void drawSignalBars(int x, int y, int tier) {
  const int barWidth = 3;
  const int gap = 2;
  const int heights[3] = {3, 6, 9};

  for (int i = 0; i < 3; i++) {
    int barX = x + i * (barWidth + gap);
    int barY = y + (heights[2] - heights[i]);

    if (i < tier) {
      display.setColor(WHITE);
      display.fillRect(barX, barY, barWidth, heights[i]);
    } else {
      display.setColor(WHITE);
      display.drawRect(barX, barY, barWidth, heights[i]);
    }
  }
}

void showLastSeen() {
  display.clear();
  display.setColor(WHITE);
  display.drawString(0, 0, "Greenhouse Monitor");

  for (int i = 0; i < MAX_NODES; i++) {
    String line = "gh" + String(nodeIds[i]) + ": ";
    int y = 14 + i * 12;

    if (lastSeen[i] == 0) {
      line += "---";
      display.drawString(0, y, line);
    } else if (millis() - lastSeen[i] < CONNECTION_TIMEOUT_MS) {
      line += String(lastRssi[i]) + "dBm";
      display.drawString(0, y, line);
      drawSignalBars(100, y + 1, signalTier(lastRssi[i], lastSnr[i]));
    } else {
      line += getSecAgo(lastSeen[i]);
      display.drawString(0, y, line);
    }
  }

  display.display();
}

void updateLastSeen(const String &received, int rssi, float snr) {
  int nodeId = parseNodeId(received);

  for (int i = 0; i < MAX_NODES; i++) {
    if (nodeId == nodeIds[i]) {
      lastSeen[i] = millis();
      lastRssi[i] = rssi;
      lastSnr[i] = snr;
      break;
    }
  }
}

void loop() {
  if (receivedFlag) {
    receivedFlag = false;

    String received;
    int state = radio.readData(received);

    if (state == RADIOLIB_ERR_NONE) {
      int rssi = (int)radio.getRSSI();
      float snr = radio.getSNR();
      reportReceived(received, rssi, snr);
      updateLastSeen(received, rssi, snr);
    } else if (state != RADIOLIB_ERR_CRC_MISMATCH) {
      Serial.printf("Receive failed, code %d\n", state);
    }

    radio.startReceive();
  }

  showLastSeen();
  delay(1000);
}
