// Енергозберігаючий вузол теплиці: прокинувся -> зміряв -> передав -> заснув.
//
// Відмінність від `greenhouse-node` (стендової прошивки) — там `loop()` крутиться
// без зупину з `delay(1000)`, і плата їсть ~79 мА, тобто ~1,6 доби від 18650.
// Тут вся робота живе в `setup()`: після передачі плата йде в deep sleep, і
// прокидається вже через reset. `loop()` не викликається ніколи.
// Розрахунки й перелік проблем — docs/power-budget.md.
//
// Інтервал сну задається на збірці (-DSLEEP_MINUTES), не в коді — див.
// platformio.ini. 5 хв ≈ 7,6 міс, 15 хв ≈ 12 міс від 3400 мАг.
//
// ВАЖЛИВО: прошивка розрахована на те, що VCC ґрунтових сенсорів перепаяно
// з постійних 3V3 на `Ve` (J2 pin 3/4, керується Vext_Ctrl). Без цієї
// перепайки сенсори їдять свої 15 мА цілодобово, і стеля ресурсу — 8 діб,
// скільки б плата не спала. Деталі — README.md поруч.

#include <Arduino.h>
#include <RadioLib.h>
#include <SSD1306Wire.h>
#include <Adafruit_SHT31.h>
#include <esp_sleep.h>
#include <esp_system.h>

// ПЕРІОД ЦИКЛУ в хвилинах — від старту до старту, а не тривалість сну.
// Плата віднімає власний час роботи, тож при вікні 30 с вона спить 14:30, і
// цикл виходить рівно 15:00. Так заміри різних вузлів лишаються синхронними.
#ifndef SLEEP_MINUTES
#define SLEEP_MINUTES 15
#endif

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

// Пінаут Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262) — той самий, що в
// greenhouse-node, плюс два піни батареї.
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

// Скільки ґрунтових сенсорів реально підключено. Число має відповідати
// залізу, а не планам: непідключений ADC-пін не читає "нуль" — він висить і
// віддає шум. Медіана з (реальне, шум, шум) повертає реальне значення лише
// тоді, коли воно випадково опинилось посередині, тобто приблизно в половині
// випадків. Зайвий сенсор у цій константі — це не запас, а зіпсований замір.
#define SOIL_SENSOR_COUNT 1

#if SOIL_SENSOR_COUNT != 1 && SOIL_SENSOR_COUNT != 3
#error "SOIL_SENSOR_COUNT: підтримується 1 (одиничний замір) або 3 (медіана)"
#endif

const int SOIL_PINS[3] = {SOIL_1, SOIL_2, SOIL_3};

#define AIR_SDA    41
#define AIR_SCL    42

// Вбудований дільник вимірювання батареї: GPIO37 вмикає його (LOW = on),
// GPIO1 читає. Тримати дільник увімкненим постійно не можна — він сам
// висаджує батарею.
#define VBAT_ADC   1
#define VBAT_CTRL  37

// Коефіцієнт дільника VBAT. Номінал для V3 — 4.9; звірити мультиметром на
// реальній платі (див. README, розділ "Калібрування VBAT").
#define VBAT_DIVIDER 4.9f

// Ємнісний сенсор віддає не частоту, а постійну напругу з піковим детектором —
// і саме конденсатор цього детектора треба зарядити після подачі живлення.
// Сам NE555/TLC555 стартує за мілісекунди, а от вихід виходить на полицю
// значно довше: у практиці називають від ~1 с до "пари секунд", хтось ставить
// 8 с із запасом.
//
// ЗАМІРЯНО 2026-08-22 (docs/калібрування-ґрунту.md): сенсор **v2.0** виходить
// на полицю за **0,1 с** за метрикою T±10, і тривалість паузи без живлення на
// це не впливає — перевірено 3 с проти 120 с. Тобто 10 000 мс тут — переплата
// приблизно в сто разів, і вона з'їдає більшу частину бюджету батареї.
//
// Константу все одно НЕ змінено, і це навмисно:
//   - заміряно на v2.0, а в цьому вузлі може стояти v1.2 (у нього T±10 = 0,4 с);
//   - заміряно в одному ґрунті й при одній температурі;
//   - deep sleep — це холодний старт УСІЄЇ плати, а не лише зняття Vext:
//     заново калібрується ADC, піднімається радіо. Чи дає він те саме число,
//     ще не перевірено.
//
// Саме це перевіряють середовища sleep_win10 / sleep_win30 / sleep_win_ab
// (етап 3a) — проти опорного вузла, який працює безперервно. Після них тут
// має стояти заміряне число.
//
// Увага: з інтервалом 5 хв 10 с прогріву — вже погана комбінація (вікно
// з'їдає 3,3% періоду, ресурс падає до ~2 міс). 10 с має сенс на 15 хв.
// Задається прапорцем збірки (див. середовища sleep_win* у platformio.ini) —
// вікно пробудження це параметр досліду, а не константа коду.
#ifndef SOIL_WARMUP_MS
#define SOIL_WARMUP_MS 10000
#endif

// Скільки тримати картинку на OLED після ручного ресету (людина в теплиці
// натиснула RESET, щоб подивитись значення). При прокиданні по таймеру OLED
// не вмикається взагалі.
// Скільки тримати підсумковий кадр перед сном. У полі 5 с достатньо — на
// екран там дивляться рідко, а кожна секунда OLED це струм. На стенді мало:
// цикл пролітає швидше, ніж людина встигає прочитати. Тому прапорцем.
#ifndef OLED_ON_MS
#define OLED_ON_MS 5000
#endif

// Кожне прокидання — свіжий boot, тож SHT31 щоразу проходить ту саму
// "притирку", через яку в журналі ловили NAN у перші цикли після ресету.
// Тому читаємо з повторами, а не один раз.
// Скільки чекати після подачі Vext, перш ніж чіпати I2C. Даташит SHT31 дає
// ~1,5 мс на старт; беремо з запасом, бо ціна помилки — цикл без повітря.
#define SENSOR_SETTLE_MS 20

// Вікно семплювання: скільки мілісекунд після пробудження вузол ПРОДОВЖУЄ
// міряти й слати, замість одного пакета на цикл.
//
// 0 — класична поведінка: один замір, одна передача, спати. Мінімум енергії,
// але й мінімум довіри до числа: один відлік нема з чим порівняти, і викид
// від наводки не відрізнити від справжньої зміни.
//
// >0 — кілька відліків за цикл, з яких потім рахується середнє. Дорожче: і
// радіо в ефірі частіше, і плата не спить довше. Свідомий обмін енергії на
// якість даних, тому число задається збіркою, а не зашите.
//
// Навмисно НЕ прив'язане до AGRO_SCREEN_TRACE: спершу воно жило разом з
// екраном, і це означало б, що в полі (де екрана нема) потік мовчки зникає.
#ifndef SAMPLE_WINDOW_MS
#define SAMPLE_WINDOW_MS 0
#endif

// Як часто повторювати замір у межах вікна. 2 с — темп опорного вузла, тож
// обидва вузли лягають на графік однаково густо.
#ifndef SAMPLE_EVERY_MS
#define SAMPLE_EVERY_MS 2000
#endif

#define SHT_RETRIES 3
#define SHT_RETRY_DELAY_MS 150

// ---------------------------------------------------------------------------
// Serial: діагностика, якої в бойовій прошивці нема
// ---------------------------------------------------------------------------
// У теплиці USB не підключений, тож кожен printf — чиста трата: форматний
// рядок займає місце у flash, символи виштовхуються в UART по ~87 мкс на
// кожен, а Serial.flush() перед сном ще й БЛОКУЄ, доки буфер не спорожніє.
// Без -DAGRO_DEBUG_SERIAL макроси розкриваються в ніщо: аргументи не обчислюються
// (тобто зникає й конкатенація String, яка інакше щоцикл лізла в купу), а
// самі рядки не потрапляють у прошивку.
//
// Увімкнути для налагодження, не чіпаючи код:
//   PLATFORMIO_BUILD_FLAGS=-DAGRO_DEBUG_SERIAL make build \
//       PROJECT=greenhouse-node-lowpower PIO_ENV=lowpower_15min
//
// Префікс AGRO_ не косметичний: Adafruit BusIO має власний макрос
// DEBUG_SERIAL, який розкривається в сам об'єкт Serial. Наш -DDEBUG_SERIAL
// перетирав його одиницею, і бібліотека переставала збиратись на
// `1.print(...)`. Прапорці збірки живуть у спільному просторі імен з усіма
// залежностями — звідси префікс.
//
// Спільного заголовка на три прошивки свідомо нема: docker-compose монтує
// кожен проєкт окремо як /project, а deploy-gateway rsync-ає тільки gateway/,
// тож файл поза каталогом проєкту зламав би і збірку в контейнері, і заливку
// на Pi. Дублювання тут дешевше за таку зв'язність.

// Діагностичне середовище існує рівно заради свого CSV — хай друкує навіть
// якщо забули дописати другий прапорець.
#if defined(SOIL_WARMUP_PROBE) && !defined(AGRO_DEBUG_SERIAL)
#define AGRO_DEBUG_SERIAL
#endif

#ifdef AGRO_DEBUG_SERIAL
#define LOG_BEGIN() Serial.begin(115200)
#define LOG(...)    Serial.printf(__VA_ARGS__)
#define LOG_LN(x)   Serial.println(x)
#define LOG_FLUSH() Serial.flush()
#else
// Аргументи навмисно не обчислюються — саме тому в LOG() не можна класти
// нічого, крім читання вже готових значень.
#define LOG_BEGIN() ((void)0)
#define LOG(...)    ((void)0)
#define LOG_LN(x)   ((void)0)
#define LOG_FLUSH() ((void)0)
#endif

// ---------------------------------------------------------------------------
// Журнал подій
// ---------------------------------------------------------------------------
// Вузол не має куди писати лог: Serial у теплиці нікуди не підключений, а
// flash (NVS) при 15 хв — це 35 тис. прокидань на рік, тобто знос сектора і
// десятки мс на 40 мА щоцикл, дорожче за сам вимір. Тому журнал живе в
// RTC-пам'яті (це просто RAM, яка живиться і в deep sleep — 0 мкДж, 0 зносу),
// а архів тримає сервер, де вже є 14-денна ротація.
//
// Слот — рівно 64 байти з 8 КБ RTC. Два представлення тих самих подій:
//   errFlags — липкий бітмаск, чиститься ЛИШЕ після вдалої передачі; переживає
//              скільки завгодно невдалих спроб, тому нічого не губиться;
//   ring     — останні 24 події з номером boot, для розбору з платою в руках.
//
// RTC_NOINIT_ATTR (а не RTC_DATA_ATTR) — щоб слот переживав ще й panic та
// watchdog-ресет, а не тільки deep sleep. Ціна — сміття після подачі
// живлення, звідси magic.

#define EV_NONE           0
#define EV_COLD_BOOT      1   // RTC-пам'ять була порожня: перше вмикання або
                              // повна втрата живлення (заміна батареї)
#define EV_RST_BROWNOUT   2   // просадка живлення — головний підозрюваний при
                              // старій комірці на морозі
#define EV_RST_PANIC      3
#define EV_RST_WDT        4
#define EV_LORA_INIT_FAIL 5
#define EV_LORA_TX_FAIL   6
#define EV_I2C_FAIL       7   // sht31.begin() не побачив сенсор на шині
#define EV_SHT_NAN        8   // сенсор на шині, але не віддає число
#define EV_SOIL_RANGE     9   // сире ADC поза правдоподібним діапазоном:
                              // обрив або КЗ ґрунтового сенсора
#define EV_VBAT_LOW       10
#define EV_VBAT_CRITICAL  11
// 12-15 вільні — код займає 4 біти в записі кільця

#define EVENT_RING_SIZE 24
#define RTC_LOG_MAGIC   0x41475230UL  // "AGR0"

// Плата гасне вже на ~3,4 В на банці (лінійний регулятор — див.
// docs/power-budget.md), тому CRITICAL ставимо трохи вище цієї межі, щоб
// встигнути отримати попередження, а не тишу.
#define VBAT_LOW_V      3.50f
#define VBAT_CRITICAL_V 3.35f

// 12-бітний ADC дає 0..4095. Відключений сенсор плаває під стелею, замкнутий
// лежить на нулі — і те, і те map() слухняно перетворить у правдоподібний
// відсоток. Межі свідомо ширші за робочий діапазон (1200..3000): ловимо
// поламане залізо, а не сухий чи мокрий ґрунт.
#define SOIL_RAW_MIN 300
#define SOIL_RAW_MAX 3800

RTC_NOINIT_ATTR struct {
  uint32_t magic;
  uint32_t bootCount;
  uint32_t errFlags;                // 1 << EV_*, липкий до вдалої передачі
  uint16_t errSeq;                  // одометр подій, не скидається ніколи
  uint16_t ring[EVENT_RING_SIZE];   // code(4 біти) | bootCount(12 бітів)
  uint8_t  head;
} rtcLog;

// Слот має лишатись рівно 64 байти: додаси поле — або зменш кільце, або
// свідомо перепиши цю цифру, а не дізнавайся про ріст випадково.
static_assert(sizeof(rtcLog) == 64, "RTC event log slot must stay 64 bytes");

// Запис у RAM — нічого не чекає, нічого не блокує. Рядок у Serial є лише в
// діагностичній збірці; у бойовій подія живе тільки в RTC і в ефірі.
void logEvent(uint8_t code) {
  rtcLog.errFlags |= (1UL << code);
  rtcLog.errSeq++;
  rtcLog.ring[rtcLog.head] =
      ((uint16_t)code << 12) | (uint16_t)(rtcLog.bootCount & 0x0FFF);
  rtcLog.head = (rtcLog.head + 1) % EVENT_RING_SIZE;

  LOG("EV %u @boot %lu\n", code, rtcLog.bootCount);
}

void initEventLog() {
  if (rtcLog.magic != RTC_LOG_MAGIC) {
    memset(&rtcLog, 0, sizeof(rtcLog));
    rtcLog.magic = RTC_LOG_MAGIC;
    rtcLog.bootCount = 1;
    logEvent(EV_COLD_BOOT);
    return;
  }
  rtcLog.bootCount++;
}

// Причина попереднього ресету лежить в апаратному регістрі й переживає навіть
// те, що стирає RTC-пам'ять. Саме тому brownout ловиться без flash.
void logResetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_BROWNOUT:
      logEvent(EV_RST_BROWNOUT);
      break;
    case ESP_RST_PANIC:
      logEvent(EV_RST_PANIC);
      break;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
      logEvent(EV_RST_WDT);
      break;
    default:
      break;  // POWERON / DEEPSLEEP / SW — рутина, не подія
  }
}

SSD1306Wire display(0x3c, OLED_SDA, OLED_SCL);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);

TwoWire airWire = TwoWire(1);
Adafruit_SHT31 sht31 = Adafruit_SHT31(&airWire);

// Момент подачі Vext — від нього рахується прогрів сенсорів. Ініціалізація
// I2C/SPI/радіо все одно займає свій час, тож прогрів іде паралельно з нею,
// а не додається зверху.
unsigned long vextOnAtMs = 0;

void powerOnVext() {
  pinMode(VEXT_CTRL, OUTPUT);
  digitalWrite(VEXT_CTRL, LOW);  // LOW = живлення подано
  vextOnAtMs = millis();
}

#ifdef AGRO_SCREEN_TRACE
// Визначено нижче, поруч із showTelemetry: там живе решта роботи з екраном, а
// сюди потрібне лише ім'я — прогрів іде раніше за будь-який показ телеметрії.
void screenStage(const String &line1, const String &line2);
#endif

void waitForSoilWarmup() {
  unsigned long elapsed = millis() - vextOnAtMs;
  if (elapsed >= SOIL_WARMUP_MS) return;

#ifdef AGRO_SCREEN_TRACE
  // Дрібними кроками з лічильником: десять секунд нерухомого екрана читаються
  // як зависання, і саме вони створювали враження, що плата не працює.
  unsigned long left;
  while ((elapsed = millis() - vextOnAtMs) < SOIL_WARMUP_MS) {
    left = (SOIL_WARMUP_MS - elapsed + 999) / 1000;
    screenStage("WAKE #" + String(rtcLog.bootCount),
                "soil warmup " + String(left) + "s");
    delay(200);
  }
#else
  delay(SOIL_WARMUP_MS - elapsed);
#endif
}

void powerOffVext() {
  digitalWrite(VEXT_CTRL, HIGH);
}

void resetDisplay() {
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(20);
  digitalWrite(OLED_RST, HIGH);
}

// Прокидання по таймеру — це рутинний цикл у порожній теплиці, там дивитись
// на екран нікому. Показуємо тільки коли людина сама натиснула RESET або
// щойно подала живлення.
bool wokeFromTimer() {
  return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
}

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

  // ЧИ ЦЕ ВЗАГАЛІ БАТАРЕЯ. Той самий захист, що в greenhouse-node.
  //
  // Поріг "нижче 2,5 В = батареї нема" не працює: на USB БЕЗ комірки дільник
  // читає шину зарядника, тобто цілком правдоподібні 3,7-4,1 В. Саме так цей
  // вузол рапортував vbat 4.02 і 4.25, стоячи на кабелі без жодної банки.
  //
  // Розрізняє їх СТАБІЛЬНІСТЬ, а не рівень: шина зарядника імпульсна й гуляє
  // на сотні мілівольт між замірами, а справжня комірка стоїть рівно.
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

  digitalWrite(VBAT_CTRL, LOW);  // дільник назад у вимкнений стан

  float volts = (mv * VBAT_DIVIDER) / 1000.0f;

  // NAN, а не нуль: "не знаємо" це не "розряджено вщент".
  if (unstable || volts < 2.5f) {
    return NAN;
  }

  // Події про низький заряд — тільки коли напруга справді відома. Інакше вузол
  // на USB щоцикл рапортував би критичний розряд неіснуючої батареї, і серед
  // цього шуму справжня подія загубилась би.
  if (volts < VBAT_CRITICAL_V) {
    logEvent(EV_VBAT_CRITICAL);
  } else if (volts < VBAT_LOW_V) {
    logEvent(EV_VBAT_LOW);
  }
  return volts;
}

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

// Останній сирий медіанний замір — щоб A/B-режим міг узяти його, не роблячи
// зайвого читання поверх штатного.
int lastSoilRaw = 0;

// Скільки мілісекунд від подачі Vext минуло до заміру ґрунту. Їде в пакеті
// навмисно: прогрів більше не ховається за мовчазною паузою, тож замість
// довіряти константі ми БАЧИМО, у який момент циклу знято число. Якщо перші
// заміри виявляться кривими, це буде видно на парі (soil_at_ms, soil_raw) на
// живих даних, а не на стенді.
unsigned long lastSoilAtMs = 0;

// Сирий медіанний замір, окремо від перетворення у відсотки — щоб A/B-режим
// міг зняти його двічі за цикл на різних відмітках прогріву.
int readSoilRawMedian() {
  int v[3] = {0, 0, 0};

  // Перший вимір після старту ADC регулярно виходить кривим (мультиплексор і
  // схема вибірки ще не встоялись) — стандартний обхід: прочитати й викинути.
  for (int i = 0; i < SOIL_SENSOR_COUNT; i++) {
    analogRead(SOIL_PINS[i]);
  }
  for (int i = 0; i < SOIL_SENSOR_COUNT; i++) {
    v[i] = analogRead(SOIL_PINS[i]);
  }

  // Сирі значення в Serial — без них калібрування наосліп: у percent вже
  // втрачено те, що потрібно для підбору меж у soilRawToPercent().
#if SOIL_SENSOR_COUNT == 3
  LOG("soil raw: %d %d %d\n", v[0], v[1], v[2]);
  lastSoilRaw = medianOf3(v[0], v[1], v[2]);
#else
  LOG("soil raw: %d (at %lu ms)\n", v[0], millis() - vextOnAtMs);
  lastSoilRaw = v[0];
#endif
  lastSoilAtMs = millis() - vextOnAtMs;
  return lastSoilRaw;
}

int readSoilPercent() {
  int raw = readSoilRawMedian();
  // Медіана з трьох переживе один відпалий сенсор мовчки — і саме тому обрив
  // треба ловити окремо, інакше він ніколи не спливе.
  if (raw < SOIL_RAW_MIN || raw > SOIL_RAW_MAX) {
    logEvent(EV_SOIL_RANGE);
  }
  return soilRawToPercent(raw);
}

#ifdef SOIL_WARMUP_PROBE
// Діагностика, не бойовий режим: малює криву виходу сенсора після подачі
// живлення, щоб побачити, за скільки він реально виходить на полицю, і
// підібрати SOIL_WARMUP_MS під свої плати замість загального 2 с.
//
// Вікно за замовчуванням 60 с — навмисне довге, бо саме воно і є відповіддю
// на питання "чи не варто працювати хвилину замість двох секунд". Якщо крива
// лягає на полицю за пару секунд — довге вікно не додає нічого, крім витрати
// батареї. Якщо повзе далі (наприклад, 555 гріється власним струмом і
// дрейфує) — значить довге вікно справді потрібне. Вирішує форма кривої,
// не аргумент.
void probeSoilWarmup() {
  LOG_LN("ms,s1,s2,s3");
  unsigned long until = vextOnAtMs + (unsigned long)SOIL_WARMUP_PROBE * 1000UL;
  while ((long)(millis() - until) < 0) {
    LOG("%lu,%d,%d,%d\n", millis() - vextOnAtMs, analogRead(SOIL_1),
        analogRead(SOIL_2), analogRead(SOIL_3));
    delay(250);
  }
}
#endif

#ifdef SOIL_AB_TEST
// Парне порівняння двох стратегій в одному циклі: замір на SOIL_WARMUP_MS і
// повторний на SOIL_AB_TEST секунді, той самий ґрунт, та сама температура,
// та сама плата. Це сильніше за порівняння двох вузлів поруч — там різниця
// сенсорів і місця в горщику змішалась би з різницею методів.
int soilFastRaw = 0;
int soilSlowRaw = 0;

void readSoilSlow() {
  unsigned long until = vextOnAtMs + (unsigned long)SOIL_AB_TEST * 1000UL;
  while ((long)(millis() - until) < 0) {
    delay(100);
  }
  soilSlowRaw = readSoilRawMedian();
}
#endif

// true — прочитали; false — сенсор мовчить, значення лишаються нулями.
bool readAir(float &airTemp, float &airHum) {
  for (int attempt = 0; attempt < SHT_RETRIES; attempt++) {
    airTemp = sht31.readTemperature();
    airHum = sht31.readHumidity();

    if (!isnan(airTemp) && !isnan(airHum)) {
      return true;
    }

    // Повторити те саме звернення до сенсора, який уже мовчить, — марно.
    // Скидання повертає його з підвислого стану, у який він міг потрапити на
    // старті живлення; без цього три спроби були трьома однаковими невдачами.
    sht31.reset();
    delay(SHT_RETRY_DELAY_MS);
  }

  // Раніше тут стояло airTemp = airHum = 0, і нулі йшли в ефір як звичайний
  // вимір: сервер писав 0.0 °C у базу і будував на них алерти. Тепер полів
  // просто нема в пакеті — "не знаємо" це не "нуль".
  LOG_LN("SHT31 read failed (NAN) after retries");
  logEvent(EV_SHT_NAN);
  return false;
}

// Формат лишається тим самим JSON, що й у greenhouse-node — gateway його
// парсить як є. Додані vbat/boot шлюз просто перекладе далі, а сервер
// (pydantic, extra="ignore") пропустить повз — див. README, "Сумісність".
//
// err/eseq додаються ТІЛЬКИ коли є що сказати: у здоровому циклі пакет не росте
// ні на байт. Навіть якби ріс — 10 байт це ~74 мс ефіру, ~0,02 мАг на добу з
// 3000, тобто не ефір тут вузьке місце (див. docs/power-budget.md).
String buildTelemetry(int soilPercent, bool airOk, float airTemp, float airHum,
                      float vbat) {
  String json = "{\"type\":\"measurement\",\"node_id\":" + String(NODE_ID);

  // Немає полів = немає даних. Приймальний бік відрізнить це від нуля.
  if (airOk) {
    json += ",\"air_temperature\":" + String(airTemp, 1);
    json += ",\"air_humidity\":" + String(airHum, 1);
  }

  json += ",\"soil_moisture\":" + String(soilPercent);
  // Сире значення — щоб шкалу можна було переглянути заднім числом; див.
  // коментар до soilRawToPercent().
  json += ",\"soil_raw\":" + String(lastSoilRaw);
  json += ",\"soil_at_ms\":" + String(lastSoilAtMs);
  // Немає поля = не знаємо, рівно як з повітрям вище. Приймальний бік
  // відрізнить це від нуля, а nullable-колонка в базі це вже вміє.
  if (!isnan(vbat)) {
    json += ",\"vbat\":" + String(vbat, 2);
  }
  json += ",\"boot\":" + String(rtcLog.bootCount);

  if (rtcLog.errFlags != 0) {
    json += ",\"err\":" + String(rtcLog.errFlags);
    json += ",\"eseq\":" + String(rtcLog.errSeq);
  }

#ifdef SOIL_AB_TEST
  json += ",\"soil_fast\":" + String(soilFastRaw);
  json += ",\"soil_slow\":" + String(soilSlowRaw);
  json += ",\"warm_fast_ms\":" + String(SOIL_WARMUP_MS);
  json += ",\"warm_slow_ms\":" + String((unsigned long)SOIL_AB_TEST * 1000UL);
#endif

  json += "}";
  return json;
}

// ---------------------------------------------------------------------------
// Стендовий показ ходу циклу (AGRO_SCREEN_TRACE)
// ---------------------------------------------------------------------------
// У бойовому режимі екран засвічується ЛИШЕ після ручного RESET
// (`if (!wokeFromTimer())` нижче), а на пробудженні за таймером не вмикається
// зовсім. Це правильна економія — OLED їсть ~10-20 мА, і на 16-секундному
// вікні кожні 5 хвилин це помітна частка добового бюджету.
//
// Але на столі наслідок неприємний: плата, яка справно шле, виглядає
// вимкненою і не реагує ні на що. Тому окремий прапорець, який показує ХІД
// циклу — прокинувся, гріє ґрунт, передав, засинає, — щоб було видно, що вона
// жива. Для теплиці не вмикати.
#ifdef AGRO_SCREEN_TRACE
bool screenReady = false;

void screenBegin() {
  if (screenReady) return;
  resetDisplay();
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  screenReady = true;
}

void screenStage(const String &line1, const String &line2) {
  screenBegin();
  display.clear();
  display.drawString(0, 0, line1);
  display.drawString(0, 14, line2);
  display.display();
}
#define SCREEN_STAGE(a, b) screenStage((a), (b))
#else
#define SCREEN_STAGE(a, b) ((void)0)
#endif

// Малює кадр і повертається одразу. Пауза й гасіння екрана — на тому, хто
// викликає: стендовий режим оновлює цей самий кадр у циклі, і власна пауза
// всередині зробила б це неможливим.
void drawTelemetryFrame(int soilPercent, bool airOk, float airTemp, float airHum,
                        float vbat, bool txOk) {
#ifdef AGRO_SCREEN_TRACE
  screenBegin();
#else
  resetDisplay();
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
#endif

  display.clear();
  display.drawString(0, 0, "AIR TEMP: " + (airOk ? String(airTemp, 1) + "C" : "--"));
  display.drawString(0, 12, "AIR HUM: " + (airOk ? String(airHum, 1) + "%" : "--"));
  // Сире ADC замість відсотка: 0..4095, менше = вологіше. Відсоток залежить
  // від калібрування, яке ще триває, і на екрані більше плутає, ніж пояснює.
  display.drawString(0, 24, "SOIL: " + String(lastSoilRaw) + " adc");
  // NAN = живимось не від комірки. "USB" чесніше за "nan" і за вигадані вольти.
  display.drawString(0, 36, "BAT: " + (isnan(vbat) ? String("USB") : String(vbat, 2) + "V"));

  // Людина стоїть біля плати з ліхтариком саме тоді, коли щось не так —
  // хай бачить, чи є непередані помилки, не чекаючи серверу.
  if (rtcLog.errFlags != 0) {
    display.drawString(0, 48, "ERR: 0x" + String(rtcLog.errFlags, HEX));
  } else {
    // Головне, заради чого людина дивиться на екран: пакет пішов чи ні.
    display.drawString(0, 48, String(txOk ? "SENT" : "TX FAIL") + " · sleep " +
                                  String(SLEEP_MINUTES) + "min");
  }
  display.display();
}

void showTelemetry(int soilPercent, bool airOk, float airTemp, float airHum,
                   float vbat, bool txOk) {
  drawTelemetryFrame(soilPercent, airOk, airTemp, airHum, vbat, txOk);
  delay(OLED_ON_MS);
  display.displayOff();
}

#if defined(AGRO_SCREEN_TRACE) || SAMPLE_WINDOW_MS > 0
// Вікно неспання: поки воно триває, вузол ПРОДОВЖУЄ міряти й слати,
// а не показує знімок.
//
// Бойова логіка — один пакет на цикл, і в цьому весь сенс сну. Але на столі
// вона робить перевірку неможливою: занурив щуп у воду — і бачиш те саме
// число, бо замір уже зроблено, а наступний буде аж наступного циклу. Тут
// цикл вимірювання й передачі крутиться далі, тож реакція сенсора видно
// одразу і на екрані, і в базі.
//
// Ціна — кілька пакетів замість одного й повне вікно радіо в ефірі. Для
// батареї це неприйнятно, тому в lowpower_5min/15min цього блоку нема.
void runAwakeWindow(int soilPercent, bool airOk, float airTemp, float airHum,
                    float vbat, bool txOk) {
  // Вікно тримається доти, доки потрібне ХОЧ ОДНОМУ зі споживачів: даним або
  // очам. Інакше при OLED_ON_MS > SAMPLE_WINDOW_MS екран гаснув би раніше,
  // ніж людина встигає прочитати, а при зворотному співвідношенні обірвався б
  // збір даних.
  unsigned long windowMs = SAMPLE_WINDOW_MS;
#ifdef AGRO_SCREEN_TRACE
  if (OLED_ON_MS > windowMs) windowMs = OLED_ON_MS;
#endif
  const unsigned long until = millis() + windowMs;

  while (true) {
#ifdef AGRO_SCREEN_TRACE
    drawTelemetryFrame(soilPercent, airOk, airTemp, airHum, vbat, txOk);
#endif
    if ((long)(millis() - until) >= 0) break;

    delay(SAMPLE_EVERY_MS);

    soilPercent = readSoilPercent();
    airOk = readAir(airTemp, airHum);
    vbat = readBatteryVolts();

    String msg = buildTelemetry(soilPercent, airOk, airTemp, airHum, vbat);
    txOk = (radio.transmit(msg) == RADIOLIB_ERR_NONE);
    if (txOk) {
      LOG_LN("Sent! " + msg);
    } else {
      LOG_LN("Send failed");
    }
  }

#ifdef AGRO_SCREEN_TRACE
  display.displayOff();
#endif
}
#endif

void enterDeepSleep() {
  // Порядок важливий: спершу приспати радіо, потім зняти Vext. SX1262 у
  // standby їсть ~1,5 мА — це в 30 разів більше за сплячий ESP32, тобто без
  // цього рядка весь сенс прошивки зникає.
  radio.sleep();
  powerOffVext();

  // ЦИКЛ, а не сон: віднімаємо час, який плата вже пропрацювала.
  //
  // Раніше тут стояв фіксований сон, і цикл виходив 15 хв ПЛЮС робота — тобто
  // 15:30 при вікні 30 с, і ще довше, якщо передача пішла з повтору. Заміри
  // від різних вузлів розповзались би в часі, а порівнювати їх треба парно.
  //
  // Тепер плата спить рівно стільки, щоб від старту до старту вийшло
  // SLEEP_MINUTES. Час роботи вимірюється сам, тож затягнуте пробудження
  // компенсується коротшим сном, а не зсуває наступний цикл.
  const uint64_t cycleUs = (uint64_t)SLEEP_MINUTES * 60ULL * 1000000ULL;
  const uint64_t awakeUs = (uint64_t)millis() * 1000ULL;

  // Якщо робота чомусь зайняла весь цикл — не лягаємо на нуль: інакше вузол
  // крутився б без сну й висадив батарею за добу.
  //
  // Межа пропорційна, і це не дрібниця. Раніше тут стояли жорсткі 60 с, і при
  // SLEEP_MINUTES=1 вони дорівнювали ЦІЛОМУ циклу: умова не спрацьовувала
  // ніколи, компенсація часу роботи мовчки вимикалась, сон завжди виходив
  // рівно 60 с — а фактичний цикл ставав 76 с при вікні 16,6 с. Лог при цьому
  // незворушно рапортував "cycle 1 min". На 15 хвилинах цього не видно, тому
  // й проїхало непоміченим до першого стендового прогону на хвилині.
  const uint64_t floorUs = 60ULL * 1000000ULL;
  const uint64_t minSleepUs = (cycleUs / 2 < floorUs) ? cycleUs / 2 : floorUs;

  uint64_t sleepUs = (awakeUs + minSleepUs < cycleUs) ? cycleUs - awakeUs
                                                      : minSleepUs;

  // Фактичний цикл, а не замовлений: якщо нижня межа таки спрацювала, різницю
  // видно одразу, і не доведеться вираховувати її з часу між рядками.
  LOG("Awake %lu ms, sleeping %llu s -> cycle %llu s (asked %d min)\n",
      millis(), sleepUs / 1000000ULL,
      (awakeUs + sleepUs) / 1000000ULL, SLEEP_MINUTES);
  LOG_FLUSH();

  esp_sleep_enable_timer_wakeup(sleepUs);
  esp_deep_sleep_start();
  // сюди виконання не повертається — наступний рядок буде вже `setup()`
}

void setup() {
  // 240 МГц тут нема на чому витрачати: вся робота — це чекання сенсорів і
  // ефіру. На 80 МГц активне споживання ядра помітно менше, а час циклу
  // майже не росте.
  setCpuFrequencyMhz(80);

  LOG_BEGIN();

  // Спершу журнал — інакше перші ж logEvent() (холодний старт, brownout)
  // писали б у неініціалізований слот.
  initEventLog();
  logResetReason();

  // Vext живить і OLED, і (після перепайки) ґрунтові сенсори — вмикаємо на
  // час замірів і знімаємо перед сном.
  powerOnVext();

  // Перший кадр — найпершим ділом, ще до будь-яких пауз та ініціалізацій.
  // Інакше між подачею живлення й початком відліку прогріву лишається
  // проміжок з темним екраном, і саме він читається як "не вмикається".
  SCREEN_STAGE("WAKE #" + String(rtcLog.bootCount), "starting...");

  // Пауза між подачею Vext і першим доторком до шини.
  //
  // Раніше її не було зовсім: powerOnVext() і одразу begin(). SHT31 після
  // подачі живлення піднімається ~1,5 мс, і опитування в цей момент лишало
  // його в непевному стані — сенсор відповідав на адресу (тобто I2C_FAIL не
  // спрацьовував), але читання поверталось NAN. Саме така картина була на
  // вузлі 2: err=258, тобто SHT_NAN без I2C_FAIL.
  //
  // У greenhouse-node цей проміжок випадково заповнював initDisplay(), тому
  // там проблеми не видно. 20 мс проти 16-секундного вікна — ніщо.
  delay(SENSOR_SETTLE_MS);

  pinMode(AIR_SDA, INPUT_PULLUP);
  pinMode(AIR_SCL, INPUT_PULLUP);
  airWire.begin(AIR_SDA, AIR_SCL);
  if (!sht31.begin(0x44)) {
    // Сенсора нема на шині взагалі — інша поломка, ніж NAN при читанні
    // (обрив шлейфа проти збою самого сенсора), тому й код інший.
    logEvent(EV_I2C_FAIL);
  }

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  int radioState = radio.begin(868.0);
  if (radioState != RADIOLIB_ERR_NONE) {
    // Радіо не піднялось — не крутимось з увімкненим живленням, а йдемо
    // спати до наступного циклу: інакше один збій з'їдає всю батарею.
    LOG("LoRa init failed, code %d\n", radioState);
    logEvent(EV_LORA_INIT_FAIL);
    enterDeepSleep();
  }

#ifdef SOIL_WARMUP_PROBE
  probeSoilWarmup();
#endif

  waitForSoilWarmup();

  int soilPercent = readSoilPercent();

#ifdef SOIL_AB_TEST
  // Штатний (швидкий) замір уже зроблено — він і лишається soil_moisture,
  // щоб бойовий тракт не змінювався. Далі тримаємо живлення й знімаємо
  // другий, повільний, для порівняння.
  soilFastRaw = lastSoilRaw;
  readSoilSlow();
#endif

  float airTemp = 0, airHum = 0;
  bool airOk = readAir(airTemp, airHum);
  float vbat = readBatteryVolts();

  String msg = buildTelemetry(soilPercent, airOk, airTemp, airHum, vbat);
  int txState = radio.transmit(msg);

  if (txState == RADIOLIB_ERR_NONE) {
    // Прапорці доїхали — тільки тепер їх можна гасити. Одометр errSeq
    // лишається рости, щоб сервер бачив повтори тієї самої помилки.
    rtcLog.errFlags = 0;
    LOG_LN("Sent! " + msg);
  } else {
    LOG("Send failed, code %d\n", txState);
    logEvent(EV_LORA_TX_FAIL);
  }

  const bool txOk = (txState == RADIOLIB_ERR_NONE);
#if defined(AGRO_SCREEN_TRACE) || SAMPLE_WINDOW_MS > 0
  runAwakeWindow(soilPercent, airOk, airTemp, airHum, vbat, txOk);
#else
  if (!wokeFromTimer()) {
    showTelemetry(soilPercent, airOk, airTemp, airHum, vbat, txOk);
  }
#endif

  enterDeepSleep();
}

void loop() {
  // Не викликається: setup() завжди завершується deep sleep-ом.
}
