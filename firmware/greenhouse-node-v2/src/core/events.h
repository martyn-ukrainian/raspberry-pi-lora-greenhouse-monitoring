#pragma once

#include <Arduino.h>

// Журнал подій у RTC-пам'яті.
//
// Вузол не має куди писати лог: Serial у теплиці нікуди не підключений, а flash
// (NVS) при 15 хв — це 35 тис. прокидань на рік, тобто знос сектора й десятки
// мс на 40 мА щоцикл, дорожче за сам вимір. Тому журнал живе в RTC-пам'яті (це
// просто RAM, яка живиться і в deep sleep — 0 мкДж, 0 зносу), а архів тримає
// сервер.
//
// Два представлення тих самих подій:
//   errFlags — липкий бітмаск, чиститься ЛИШЕ після вдалої передачі; переживає
//              скільки завгодно невдалих спроб, тому нічого не губиться;
//   ring     — останні 24 події з номером boot, для розбору з платою в руках.
//
// Коди збігаються з v1 навмисно: сервер їх уже розбирає (`decode_flags()` в
// адаптері), і розійтись тут означало б мовчки зламати розшифровку подій.

#define EV_NONE             0
#define EV_COLD_BOOT        1   // RTC-пам'ять була порожня: перше вмикання або
                                // повна втрата живлення (заміна батареї)
#define EV_RST_BROWNOUT     2   // просадка живлення
#define EV_RST_PANIC        3
#define EV_RST_WDT          4
#define EV_LORA_INIT_FAIL   5
#define EV_LORA_TX_FAIL     6
#define EV_I2C_FAIL         7   // sht31.begin() не побачив сенсор на шині
#define EV_SHT_NAN          8   // сенсор на шині, але не віддає число
#define EV_SOIL_RANGE       9   // сире ADC поза правдоподібним діапазоном
#define EV_VBAT_LOW         10
#define EV_VBAT_CRITICAL    11
#define EV_RADIO_SLEEP_FAIL 12  // radio.sleep() повернув помилку: чип лишився
                                // в standby (сотні мкА проти одиниць)
// 13-15 вільні — код займає 4 біти в записі кільця

#define EVENT_RING_SIZE 24

struct RtcLog {
  uint32_t magic;
  uint32_t bootCount;
  uint32_t errFlags;                // 1 << EV_*, липкий до вдалої передачі
  uint16_t errSeq;                  // одометр подій, не скидається ніколи
  uint16_t ring[EVENT_RING_SIZE];   // code(4 біти) | bootCount(12 бітів)
  uint8_t  head;
};

// Слот має лишатись рівно 64 байти: додаси поле — або зменш кільце, або
// свідомо перепиши цю цифру, а не дізнавайся про ріст випадково.
static_assert(sizeof(RtcLog) == 64, "RTC event log slot must stay 64 bytes");

extern RtcLog rtcLog;

void logEvent(uint8_t code);

// Готує слот. Викликати ПЕРШИМ у setup(): до цього будь-який logEvent() писав
// би в неініціалізовану пам'ять.
void initEventLog();

// Причина попереднього ресету лежить в апаратному регістрі й переживає навіть
// те, що стирає RTC-пам'ять. Саме тому brownout ловиться без flash.
void logResetReason();
