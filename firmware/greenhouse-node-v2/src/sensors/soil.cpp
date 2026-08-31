#include "sensors/soil.h"

#include "core/board.h"
#include "core/events.h"
#include "core/log.h"
#include "core/power.h"

// 12-бітний ADC дає 0..4095. Відключений сенсор плаває під стелею, замкнутий
// лежить на нулі — і те, і те шкала слухняно перетворила б у правдоподібний
// відсоток. Межі свідомо ширші за робочий діапазон (1200..3000): ловимо
// поламане залізо, а не сухий чи мокрий ґрунт.
#define SOIL_RAW_MIN 300
#define SOIL_RAW_MAX 3800

// Межі й форма — ЗАМІРЯНІ (docs/калібрування-ґрунту.md):
//
//   2874 raw — повітряно-сухий тепличний ґрунт, стіл 2026-08-22
//   1050 raw — грядка після звичайного поливу, теплиця 2026-08-23
//   ~1043 raw — асимптота: показник, у який упирається насичення
//
// ЧОМУ НЕ map(). Відгук сенсора експоненційний: у сухому ґрунті 1 мл води
// зсуває показник на 12 мВ, у мокрому — на 0,05 мВ. Тобто лінійна шкала по
// напрузі НЕ лінійна по воді: від 1200 до 1050 raw у ній лише 8 відсотків, хоча
// це більша частина води.
//
// Коефіцієнт k з моделі mV = C + A*exp(-V/k) тут СКОРОЧУЄТЬСЯ — потрібна лише
// асимптота C. Це важливо, бо k залежить від об'єму ґрунту (заміряний на 1 кг
// у відрі й на грядку не переноситься), а C — властивість сенсора й ґрунту.
#define SOIL_DRY_RAW    2874
#define SOIL_WET_RAW    1050
#define SOIL_ASYMPTOTE  1043

int soilRawToPercent(int raw) {
  // Нижче асимптоти логарифм не визначений — це вже поза шкалою.
  if (raw <= SOIL_ASYMPTOTE + 1) return 100;
  if (raw >= SOIL_DRY_RAW)       return 0;

  const float span = (float)(SOIL_DRY_RAW - SOIL_ASYMPTOTE);
  const float full = logf((float)(SOIL_WET_RAW - SOIL_ASYMPTOTE) / span);
  const float here = logf((float)(raw - SOIL_ASYMPTOTE) / span);

  return constrain((int)(100.0f * here / full), 0, 100);
}

// Один сирий відлік.
static int readSoilOnce() {
  // Перший вимір після старту ADC регулярно виходить кривим (мультиплексор і
  // схема вибірки ще не встоялись) — прочитати й викинути.
  analogRead(SOIL_1);
  return analogRead(SOIL_1);
}

#ifdef SOIL_PROBE_MS
void soilProbe() {
  LOG_LN("ms,raw");
  unsigned long until = vextOnAtMs + (unsigned long)SOIL_PROBE_MS;
  while ((long)(millis() - until) < 0) {
    // readSoilOnce() робить холостий замір перед робочим — саме так, як у
    // бойовому циклі. Інакше крива описувала б інший процес, ніж той, який ми
    // потім налаштовуємо.
    LOG("%lu,%d\n", millis() - vextOnAtMs, readSoilOnce());
    delay(SOIL_PROBE_EVERY_MS);
  }
}
#endif

SoilBurst readSoilBurst() {
  // Прогрів рахується від подачі Vext, тож ініціалізація I2C і радіо вже пішла
  // паралельно з ним, а не додалась зверху.
  unsigned long elapsed = millis() - vextOnAtMs;
  if (elapsed < SOIL_WARMUP_MS) {
    delay(SOIL_WARMUP_MS - elapsed);
  }

  // Викинуті відліки — теж повноцінні читання з тією ж паузою: нас цікавить
  // стан сенсора в часі, а не швидкість циклу.
  for (int i = 0; i < SOIL_DISCARD_FIRST; i++) {
    readSoilOnce();
    delay(SOIL_GAP_MS);
  }

  int v[SOIL_SAMPLES];

  SoilBurst b;
  b.firstAtMs = millis() - vextOnAtMs;
  b.count = SOIL_SAMPLES;

  for (int i = 0; i < SOIL_SAMPLES; i++) {
    v[i] = readSoilOnce();
    LOG("soil[%d] = %d (at %lu ms)\n", i, v[i], millis() - vextOnAtMs);
    if (i + 1 < SOIL_SAMPLES) delay(SOIL_GAP_MS);
  }

  // Сортування вставками — на трьох-п'яти елементах це і найшвидше, і
  // найзрозуміліше. qsort тут коштував би дорожче за сам замір.
  for (int i = 1; i < SOIL_SAMPLES; i++) {
    int key = v[i], j = i - 1;
    while (j >= 0 && v[j] > key) { v[j + 1] = v[j]; j--; }
    v[j + 1] = key;
  }

  b.minimum = v[0];
  b.maximum = v[SOIL_SAMPLES - 1];
  b.median  = v[SOIL_SAMPLES / 2];

  // Обрив або КЗ ловимо окремо: медіана переживе відпалий канал мовчки, і без
  // цієї перевірки поломка ніколи не спливе.
  if (b.median < SOIL_RAW_MIN || b.median > SOIL_RAW_MAX) {
    logEvent(EV_SOIL_RANGE);
  }
  return b;
}
