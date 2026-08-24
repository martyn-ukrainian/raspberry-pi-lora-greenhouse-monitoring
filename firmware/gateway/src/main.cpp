#include <Arduino.h>
#include <RadioLib.h>
#include <SSD1306Wire.h>
#include <esp_system.h>

#ifdef AGRO_WIFI
#include <WiFi.h>
#include <ESPmDNS.h>
#endif

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

// Скільки тиші вважати втратою зв'язку. Це не властивість шлюза, а властивість
// парку вузлів: рахується від того, як рідко шле НАЙРІДШИЙ з них.
//
// Стендовий вузол шле раз/сек, і 10 с тиші справді означають обрив. Але вузол
// зі сном 15 хв мовчить 900 с у штатному режимі — з жорсткими 10 с шлюз
// показував би його пропалим 99,9% часу, і реальна втрата зв'язку загубилась
// би серед хибних.
//
// Звідси прапорець збірки замість константи:
//   pio run -e gateway                       -> 12,5 с (вузли раз/сек)
//   PLATFORMIO_BUILD_FLAGS=-DNODE_INTERVAL_S=900 pio run  -> 37,5 хв
//
// Коефіцієнт 2,5 дає запас на один пропущений пакет плюс дрейф таймера сну.
// Впливає ТІЛЬКИ на екран (RSSI проти "N сек тому") — прийом пакетів від
// нього не залежить.
#ifndef NODE_INTERVAL_S
#define NODE_INTERVAL_S 1
#endif

#define CONNECTION_TIMEOUT_MS ((unsigned long)NODE_INTERVAL_S * 2500UL + 10000UL)

// ---------------------------------------------------------------------------
// Події шлюза
// ---------------------------------------------------------------------------
// Шлюз сидить на USB Pi, тож постійна пам'ять йому не потрібна — його "диск"
// це сервер. Але Serial.printf() з вільним текстом там осідав як
// "Skipping malformed packet" (usb_adapter.py), тобто справжній збій радіо
// маскувався під сміття в порту. Тому збої йдуть тим самим NDJSON, що й
// виміри, просто з type="event".
//
// Нумерація кодів своя, не спільна з вузлом — тому й node_id окремий.
// Serial тут — НЕ лог, а канал даних на Pi, тому його не можна відключати
// прапорцем збірки, як у вузлі. Натомість у потоці не має бути жодного
// вільного тексту: усе, що не NDJSON, adapter відкидає як сміття. Саме тому
// замість Serial.println("LoRa init OK") тут подія з кодом.
#define GW_NODE_ID        255
#define GWEV_LORA_INIT    1
#define GWEV_RX_FAIL      2   // detail = код RadioLib
#define GWEV_CRC_BURST    3   // detail = скільки битих пакетів усього
#define GWEV_BOOT         4   // detail = esp_reset_reason()
#define GWEV_SINK_DROP    5   // detail = скільки рядків втрачено при обриві

// CRC-mismatch — рядове явище в ефірі, по події на кожен залив би лог. Але
// повністю ковтати його теж не можна: саме темп їх появи показує, що зв'язок
// сиплеться. Компроміс — рахувати, а доповідати пачками.
#define CRC_REPORT_EVERY 10

SSD1306Wire display(0x3c, OLED_SDA, OLED_SCL);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);

unsigned long crcErrors = 0;

// ---------------------------------------------------------------------------
// Другий отримувач потоку: TCP-сокет (AGRO_WIFI)
// ---------------------------------------------------------------------------
// Приймач шлюза — Pi, і донедавна єдиною дорогою туди був USB. Виявилось, що
// шнур, яким плата під'єднана до малини, зарядний: лінії даних у ньому не
// розведені, тому `lsusb` на Pi порожній, хоча плата живиться нормально.
//
// Замість шукати інший шнур потік дублюється в мережу. Ключове рішення —
// НЕ переносити сюди логіку `usb_adapter.py`: розбір NDJSON, мапінг номера
// вузла в мітку (`config/nodes.yaml`) і розкладання бітмаска `err` на окремі
// події лишаються в Python, де вони вже написані й покриті типами. Шлюз шле
// в сокет ті самі байти, що й у Serial, а адаптер лише міняє джерело —
// serial.readline() на socket.makefile().
//
// Serial при цьому НЕ вимикається: коли плату приносять до комп'ютера з
// нормальним шнуром, той самий потік видно як раніше.
#ifdef AGRO_WIFI

#ifndef WIFI_SSID
#error "AGRO_WIFI без -DWIFI_SSID: облікові дані передаються прапорцем, у репозиторії їм не місце"
#endif
#ifndef WIFI_PASS
#error "AGRO_WIFI без -DWIFI_PASS"
#endif

// Ім'я, а не адреса, за замовчуванням: IP малини вже їздив по DHCP (.88 -> .104),
// і кожен переїзд означав би перепрошивку. Ім'я в домені .local розв'язує mDNS.
#ifndef SINK_HOST
#define SINK_HOST "agro-pi.local"
#endif
#ifndef SINK_PORT
#define SINK_PORT 9009
#endif

// Скільки рядків тримати, поки сокета нема. 24 — це приблизно хвилина потоку
// від безперервного вузла або шість годин від сплячого з 15-хвилинним циклом.
// Більше сенсу не має: довгий обрив однаково втрачає дані, і чесніше сказати
// про це подією, ніж роздувати буфер.
#define SINK_BACKLOG            24
#define SINK_RETRY_MS           5000
#define SINK_CONNECT_TIMEOUT_MS 1500
#define MDNS_QUERY_TIMEOUT_MS   2000
#define WIFI_CONNECT_TIMEOUT_MS 15000

WiFiClient sink;
IPAddress sinkIp;
bool sinkIpKnown = false;
bool mdnsStarted = false;
bool sinkEverTried = false;
unsigned long lastSinkTryMs = 0;

String backlog[SINK_BACKLOG];
int backlogHead = 0;
int backlogCount = 0;
unsigned long backlogDropped = 0;

void backlogPush(const String &line) {
  if (backlogCount == SINK_BACKLOG) {
    // Черга повна — витісняємо найстаріше. Свіжий вимір цінніший за давній:
    // під час обриву важливіше донести те, що сталось щойно.
    backlogHead = (backlogHead + 1) % SINK_BACKLOG;
    backlogCount--;
    backlogDropped++;
  }
  backlog[(backlogHead + backlogCount) % SINK_BACKLOG] = line;
  backlogCount++;
}

void sinkFlush() {
  // Про втрату повідомляємо ПЕРШИМ рядком після відновлення. Інакше провал у
  // даних на сервері виглядав би як тиша в ефірі, тобто як несправність
  // вузла — а причина була в мережі. Той самий принцип, що з CRC_BURST:
  // не мовчати про те, чого в самих даних не видно.
  if (backlogDropped > 0) {
    sink.printf("{\"type\":\"event\",\"node_id\":%d,\"code\":%d,\"detail\":%lu}\n",
                GW_NODE_ID, GWEV_SINK_DROP, backlogDropped);
    backlogDropped = 0;
  }

  while (backlogCount > 0) {
    if (sink.println(backlog[backlogHead]) == 0) return;  // не пішло — лишаємо в черзі
    backlog[backlogHead] = String();                      // звільняємо рядок одразу
    backlogHead = (backlogHead + 1) % SINK_BACKLOG;
    backlogCount--;
  }
}

bool resolveSink() {
  if (sinkIpKnown) return true;

  // Літерал адреси приймаємо як є — це шлях для мереж без mDNS.
  if (sinkIp.fromString(SINK_HOST)) {
    sinkIpKnown = true;
    return true;
  }

  // Звичайний DNS у домені .local не працює: імена там роздає mDNS, і питати
  // треба саме його. Суфікс відрізаємо, бо queryHost() хоче голе ім'я.
  String host = SINK_HOST;
  if (host.endsWith(".local")) host = host.substring(0, host.length() - 6);

  IPAddress found = MDNS.queryHost(host, MDNS_QUERY_TIMEOUT_MS);
  if ((uint32_t)found == 0) return false;

  sinkIp = found;
  sinkIpKnown = true;
  return true;
}

// Викликається раз на прохід loop(). Нічого довгого тут бути не може: поки ми
// тут, прийнятий пакет чекає у буфері SX1262, а він уміщає рівно один.
void sinkService() {
  if (WiFi.status() != WL_CONNECTED) {
    if (sink.connected()) sink.stop();
    // Перепідключенням займається сам ESP у фоні (WiFi.begin() уже викликаний).
    // Наше завдання — не вдавати, що сокет живий, і забути адресу: після
    // повернення мережі малина може приїхати з іншою.
    sinkIpKnown = false;
    return;
  }

  if (!mdnsStarted) {
    MDNS.begin("agro-gateway");
    mdnsStarted = true;
  }

  if (sink.connected()) return;

  if (sinkEverTried && (long)(millis() - lastSinkTryMs) < SINK_RETRY_MS) return;
  sinkEverTried = true;
  lastSinkTryMs = millis();

  if (!resolveSink()) return;

  if (!sink.connect(sinkIp, SINK_PORT, SINK_CONNECT_TIMEOUT_MS)) {
    // Могли достукатись за старою адресою — наступного разу перепитаємо mDNS.
    sinkIpKnown = false;
    return;
  }

  sinkFlush();
}

void wifiBegin() {
  WiFi.mode(WIFI_STA);
  // Шлюз живиться від мережі, економити нема на чому, а сон радіо додає
  // затримок і губить перші пакети після простою.
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long until = millis() + WIFI_CONNECT_TIMEOUT_MS;
  while (WiFi.status() != WL_CONNECTED && (long)(millis() - until) < 0) {
    delay(100);
  }
}

// Що показувати на екрані про стан каналу. Шлюз стоїть біля малини без
// клавіатури й монітора, тож екран — єдиний спосіб побачити, чи дані взагалі
// доїжджають, не заходячи по SSH.
const char *sinkLabel() {
  if (WiFi.status() != WL_CONNECTED) return "wifi?";
  if (!sink.connected()) return "net?";
  return "net";
}

#endif  // AGRO_WIFI

// Єдина точка виводу: усе, що шлюз каже назовні, проходить тут. Serial лишався
// б достатнім, поки приймач висить на USB; з появою мережевого каналу дублювати
// довелось би в кожному місці виводу, і рано чи пізно одне з них забулось би.
void emitLine(const String &line) {
  Serial.println(line);
#ifdef AGRO_WIFI
  // Порядок важливий: connected() перевіряємо ПЕРЕД println(), інакше запис у
  // мертвий сокет мовчки з'їв би рядок. Нуль записаних байтів — теж відмова.
  if (!sink.connected() || sink.println(line) == 0) {
    backlogPush(line);
  }
#endif
}

void emitEvent(int code, int detail) {
  char buf[96];
  snprintf(buf, sizeof(buf),
           "{\"type\":\"event\",\"node_id\":%d,\"code\":%d,\"detail\":%d}",
           GW_NODE_ID, code, detail);
  emitLine(buf);
}

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

#ifdef AGRO_WIFI
  // Мережа піднімається ДО радіо навмисно: тоді подія про завантаження йде вже
  // по готовому каналу, а не лягає в чергу. Пауза до 15 с тут нічого не варта —
  // вузли шлють безперервно, перший же наступний пакет буде прийнято.
  showStatus("WiFi...", WIFI_SSID);
  wifiBegin();
  sinkService();
  if (WiFi.status() == WL_CONNECTED) {
    showStatus("WiFi: OK", WiFi.localIP().toString());
  } else {
    showStatus("WiFi: FAIL", "check SSID/pass");
  }
#endif

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  int state = radio.begin(868.0);

  if (state == RADIOLIB_ERR_NONE) {
    // Шлюз живиться від Pi і мовчки перезавантажується (просадка по USB,
    // випав кабель) — а зовні це виглядає лише як провал у даних. Подія на
    // старті робить рестарт видимим, і detail одразу каже, від чого він був.
    emitEvent(GWEV_BOOT, esp_reset_reason());
    showStatus("LoRa: OK", "868 MHz");
  } else {
    emitEvent(GWEV_LORA_INIT, state);
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
  emitLine(withSignal);
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
#ifdef AGRO_WIFI
  // Повний заголовок займає майже всі 128 px і наліз би на індикатор каналу.
  display.drawString(0, 0, "Greenhouse");
  display.drawString(96, 0, sinkLabel());
#else
  display.drawString(0, 0, "Greenhouse Monitor");
#endif

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
    } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
      crcErrors++;
      if (crcErrors % CRC_REPORT_EVERY == 0) {
        emitEvent(GWEV_CRC_BURST, crcErrors);
      }
    } else {
      emitEvent(GWEV_RX_FAIL, state);
    }

    radio.startReceive();
  }

#ifdef AGRO_WIFI
  sinkService();
#endif

  showLastSeen();
  delay(1000);
}
