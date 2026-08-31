#include "core/power.h"

#include <esp_sleep.h>

#include "core/board.h"
#include "config.h"
#include "core/log.h"
#include "telemetry/radio_link.h"

unsigned long vextOnAtMs = 0;

void powerOnVext() {
  pinMode(VEXT_CTRL, OUTPUT);
  digitalWrite(VEXT_CTRL, LOW);  // LOW = живлення подано
  vextOnAtMs = millis();
}

void powerOffVext() {
  digitalWrite(VEXT_CTRL, HIGH);
}

// Порядок пінів і сам прийом — з ropg/heltec_esp32_lora_v3, функція
// heltec_deep_sleep(). Бібліотека заявляє 24 мкА на цій платі.
//
// МЕХАНІЗМ ВИТОКУ: пін, що лишився виходом, продовжує тримати рівень на вході
// вже сплячої мікросхеми й живить її вхідні каскади через захисні діоди. Тече
// не ESP32 — тече через SX1262 і SSD1306, які формально вимкнені.
//
// Заміряно на нашому екземплярі 30.08.2026: 0,84 мА без цього розбору, 0,01 мА
// з ним. Це найдорожчий рядок коду в проєкті, і його не можна загубити при
// жодному рефакторингу.
//
// VBAT_CTRL у списку НЕМА свідомо, хоч у ropg він є: readBatteryVolts()
// наприкінці явно кладе його в LOW — це вимкнений дільник. Відпустити означало
// б лишити затвор спливати; ціна помилки — 8 мкА, тобто на тлі 10 мкА сну не
// дрібниця.
static void releasePinsForSleep() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  const int idlePins[] = {VBAT_ADC, LED_PIN,
                          LORA_DIO1, LORA_RST, LORA_BUSY, LORA_NSS,
                          LORA_MISO, LORA_MOSI, LORA_SCK,
                          OLED_SDA, OLED_SCL, OLED_RST,
                          AIR_SDA, AIR_SCL,
                          SOIL_1};
  for (unsigned i = 0; i < sizeof(idlePins) / sizeof(idlePins[0]); i++) {
    pinMode(idlePins[i], INPUT);
  }
}

void enterDeepSleep() {
  // Порядок важливий: спершу приспати радіо, потім зняти Vext, і аж потім
  // відпускати піни — розбір має йти після того, як усе вимкнено.
  radioSleep();
  powerOffVext();
  releasePinsForSleep();

  // ЦИКЛ, а не сон: віднімаємо час, який плата вже пропрацювала, щоб від старту
  // до старту вийшло рівно SLEEP_MINUTES. Інакше цикл був би 15 хв ПЛЮС робота,
  // і заміри різних вузлів розповзались би в часі, а порівнювати їх треба
  // парно.
  const uint64_t cycleUs = (uint64_t)SLEEP_MINUTES * 60ULL * 1000000ULL;
  const uint64_t awakeUs = (uint64_t)millis() * 1000ULL;

  // Якщо робота чомусь зайняла весь цикл — не лягаємо на нуль: інакше вузол
  // крутився б без сну й висадив батарею за добу.
  //
  // Межа ПРОПОРЦІЙНА, і це не дрібниця. Раніше тут стояли жорсткі 60 с, і при
  // SLEEP_MINUTES=1 вони дорівнювали цілому циклу: умова не спрацьовувала
  // ніколи, компенсація мовчки вимикалась, а лог незворушно рапортував
  // "cycle 1 min" при фактичних 76 с.
  const uint64_t floorUs = 60ULL * 1000000ULL;
  const uint64_t minSleepUs = (cycleUs / 2 < floorUs) ? cycleUs / 2 : floorUs;

  uint64_t sleepUs = (awakeUs + minSleepUs < cycleUs) ? cycleUs - awakeUs
                                                      : minSleepUs;

  // Фактичний цикл, а не замовлений: якщо нижня межа таки спрацювала, різницю
  // видно одразу. Заразом це головна перевірка бюджету вікна — `Awake` має
  // збігатись з розрахунком, інакше вся арифметика ресурсу їде.
  LOG("Awake %lu ms, sleeping %llu s -> cycle %llu s (asked %d min)\n",
      millis(), sleepUs / 1000000ULL,
      (awakeUs + sleepUs) / 1000000ULL, SLEEP_MINUTES);
  LOG_FLUSH();

  esp_sleep_enable_timer_wakeup(sleepUs);
  esp_deep_sleep_start();
  // сюди виконання не повертається — наступний рядок буде вже setup()
}
