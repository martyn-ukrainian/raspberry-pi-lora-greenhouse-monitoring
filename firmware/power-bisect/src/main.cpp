// Бісекція струму сну Heltec WiFi LoRa 32 V3.
//
// Питання, заради якого написано: вузол у deep sleep їсть 0,94 мА замість
// очікуваних 0,135. Розбір, звідки взялись обидва числа і які є підозрювані —
// `docs/струм-сну.md`; тут лише інструмент.
//
// Ідея проста: замість гадати, зібрати чотири прошивки, де кожна наступна
// відрізняється від попередньої рівно одним доданим блоком ініціалізації, і
// подивитись, на якому кроці число стрибне.
//
//   AGRO_STEP=0  неспання, порожній loop() — опорна точка «плата працює»
//   AGRO_STEP=1  A: тільки esp_deep_sleep_start(), більше нічого
//   AGRO_STEP=2  B: A + SPI + radio.begin() + radio.sleep()
//   AGRO_STEP=3  C: B + Vext + обидві шини I2C + дисплей
//   AGRO_STEP=4  E: порядок укладання спати з ropg/heltec_esp32_lora_v3
//
// Крок E стоїть окремо від бісекції: це не «ще один доданий блок», а чужий
// рецепт цілком, узятий як є. Бібліотека ropg заявляє 24 мкА на цій самій
// платі при пробудженні за таймером; якщо на нашому екземплярі вийде те
// саме — виток був у прошивці, і його адреса відома. Якщо лишиться 0,84 —
// рецепт вичерпано, далі тільки залізо.
//
// Умова коректності заміру: між кроками не міняється НІЧОГО, крім прошивки —
// та сама банка, той самий прилад, той самий діапазон, ті самі дроти.

#include <Arduino.h>

#if AGRO_STEP >= 2
#include <RadioLib.h>
#include <SPI.h>
#endif

// Крок E чіпає піни, яких решта кроків не знає: світлодіод і дільник батареї.
#define LED_PIN    35
#define VBAT_CTRL  37
#define VBAT_ADC   1

#if AGRO_STEP == 3
#include <SSD1306Wire.h>
#include <Wire.h>
#endif

// Пінаут — той самий, що в greenhouse-node-lowpower/src/main.cpp. Свідомо
// продубльовано, а не винесено в спільний заголовок: цей проєкт має лишатись
// таким, щоб його можна було прочитати цілком за хвилину.
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

#define AIR_SDA    41
#define AIR_SCL    42

// Скільки спить крок A/B/C між пробудженнями. Спати «назавжди» було б чесніше
// за задумом, але плата тоді виглядає мертвою, і не відрізнити «спить як
// треба» від «зависла при старті». Раз на 10 хвилин вона прокидається, нічого
// не робить і лягає назад — на показ приладу це не впливає, зате видно, що
// вузол живий.
#ifndef WAKE_MINUTES
#define WAKE_MINUTES 10
#endif

#ifdef AGRO_DEBUG_SERIAL
#define LOG_BEGIN() Serial.begin(115200)
#define LOG(...)    Serial.printf(__VA_ARGS__)
#define LOG_FLUSH() Serial.flush()
#else
#define LOG_BEGIN() ((void)0)
#define LOG(...)    ((void)0)
#define LOG_FLUSH() ((void)0)
#endif

#if AGRO_STEP >= 2
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
#endif

#if AGRO_STEP == 3
SSD1306Wire display(0x3c, OLED_SDA, OLED_SCL);
TwoWire airWire = TwoWire(1);
#endif

#if AGRO_STEP >= 1
void enterDeepSleep() {
  LOG("step %d: sleeping %d min\n", AGRO_STEP, WAKE_MINUTES);
  LOG_FLUSH();
  esp_sleep_enable_timer_wakeup((uint64_t)WAKE_MINUTES * 60ULL * 1000000ULL);
  esp_deep_sleep_start();
}
#endif

void setup() {
#ifdef FORCE_VEXT_OFF
  // Перший рядок до всього іншого: зняти Vext, тобто погасити OLED і все, що
  // висить на `Ve`. Кроки A і 0 самі GPIO36 не чіпають взагалі — там міряється
  // підлога «прошивка не робить нічого», разом із тим станом, у якому Vext
  // лишає сама плата після скидання. Це правильно для підлоги, але не дає
  // відповіді на питання «а це часом не екран». Прапорець дає: те саме
  // середовище, мінус живлення периферії, різниця двох чисел і є ціна екрана.
  pinMode(VEXT_CTRL, OUTPUT);
  digitalWrite(VEXT_CTRL, HIGH);  // HIGH = живлення знято
#endif

  LOG_BEGIN();

#if defined(CPU_MHZ)
  setCpuFrequencyMhz(CPU_MHZ);
#endif

#if AGRO_STEP == 3
  // Vext вмикається так само, як у бойовій: LOW = живлення подано. Тут воно
  // нікому не потрібне (сенсорів нема), і саме в цьому сенс — перевіряється
  // не сенсор, а сам MOSFET і те, чи справді зняття Vext повертає лінію в
  // нуль.
  pinMode(VEXT_CTRL, OUTPUT);
  digitalWrite(VEXT_CTRL, LOW);
  delay(50);

  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(20);
  digitalWrite(OLED_RST, HIGH);

  display.init();
  display.flipScreenVertically();

  pinMode(AIR_SDA, INPUT_PULLUP);
  pinMode(AIR_SCL, INPUT_PULLUP);
  airWire.begin(AIR_SDA, AIR_SCL);
#endif

#if AGRO_STEP >= 2
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  int beginState = radio.begin(868.0);
  LOG("radio.begin -> %d\n", beginState);
#endif

#if AGRO_STEP == 4
  // Далі — рецепт ropg/heltec_esp32_lora_v3 у його власному порядку.
  //
  // sleep(false) — це ХОЛОДНИЙ сон SX1262: конфігурація не зберігається,
  // зате чип іде в 160 нА замість 0,6-1,2 мкА теплого. Для нас втрата
  // конфігурації безкоштовна — бойова прошивка все одно налаштовує радіо
  // заново після кожного пробудження, бо deep sleep це холодний старт.
  int sleepState = radio.sleep(false);
  LOG("radio.sleep(false) -> %d\n", sleepState);

  pinMode(VEXT_CTRL, OUTPUT);
  digitalWrite(VEXT_CTRL, HIGH);  // HIGH = живлення периферії знято

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Головне, чого нема в бойовій прошивці: жоден пін не лишається виходом.
  // Пін, який тримає рівень на вході сплячої мікросхеми, живить її вхідні
  // каскади — і саме це в чужих замірах відділяло сотні мікроампер від
  // десятків.
  const int idlePins[] = {VBAT_CTRL, VBAT_ADC, LED_PIN,
                          LORA_DIO1, LORA_RST, LORA_BUSY, LORA_NSS,
                          LORA_MISO, LORA_MOSI, LORA_SCK,
                          OLED_SDA, OLED_SCL, OLED_RST,
                          AIR_SDA, AIR_SCL};
  for (unsigned i = 0; i < sizeof(idlePins) / sizeof(idlePins[0]); i++) {
    pinMode(idlePins[i], INPUT);
  }
#endif

#if AGRO_STEP == 3
  display.displayOff();
  digitalWrite(VEXT_CTRL, HIGH);  // HIGH = живлення знято
#endif

#if AGRO_STEP == 2 || AGRO_STEP == 3
  // Код повернення тут перевіряється навмисне: у бойовій прошивці
  // `radio.sleep()` викликається без перевірки, і якщо чип був зайнятий,
  // він лишається в standby (~1,5 мА), а прошивка про це не знає. Одна з
  // гіпотез документа — саме ця. Побачити її можна лише на діагностичному
  // прогоні по USB (-DAGRO_DEBUG_SERIAL).
  int sleepState = radio.sleep();
  LOG("radio.sleep -> %d\n", sleepState);
#endif

#if AGRO_STEP >= 1
  enterDeepSleep();
#endif
}

void loop() {
  // Сюди потрапляє лише AGRO_STEP=0: плата не спить ніколи і не робить
  // нічого. delay() не вводить light sleep, тобто ядро крутиться весь час —
  // це й треба заміряти.
  delay(1000);
}
