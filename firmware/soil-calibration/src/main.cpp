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
