#include "core/events.h"

#include <esp_system.h>

#include "core/log.h"

#define RTC_LOG_MAGIC 0x41475232UL  // "AGR2" — інший, ніж у v1: RTC-пам'ять
                                    // спільна, і чужий слот треба прочитати як
                                    // сміття, а не як свій

// RTC_NOINIT_ATTR (а не RTC_DATA_ATTR) — щоб слот переживав ще й panic та
// watchdog-ресет, а не тільки deep sleep. Ціна — сміття після подачі живлення,
// звідси magic.
RTC_NOINIT_ATTR RtcLog rtcLog;

// Запис у RAM — нічого не чекає, нічого не блокує.
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

void logResetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_BROWNOUT: logEvent(EV_RST_BROWNOUT); break;
    case ESP_RST_PANIC:    logEvent(EV_RST_PANIC);    break;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      logEvent(EV_RST_WDT);      break;
    default: break;  // POWERON / DEEPSLEEP / SW — рутина, не подія
  }
}
