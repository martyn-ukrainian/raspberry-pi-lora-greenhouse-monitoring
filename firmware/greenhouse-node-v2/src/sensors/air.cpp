#include "sensors/air.h"

#include <Adafruit_SHT31.h>
#include <Wire.h>

#include "config.h"
#include "core/board.h"
#include "core/events.h"
#include "core/log.h"
#include "core/power.h"   // vextOnAtMs — нульова точка кривої

static TwoWire airWire = TwoWire(1);
static Adafruit_SHT31 sht31 = Adafruit_SHT31(&airWire);

void airBegin() {
  pinMode(AIR_SDA, INPUT_PULLUP);
  pinMode(AIR_SCL, INPUT_PULLUP);
  airWire.begin(AIR_SDA, AIR_SCL);
  if (!sht31.begin(0x44)) {
    logEvent(EV_I2C_FAIL);
  }
}

#ifdef AIR_PROBE_MS
void airProbe() {
  LOG_LN("ms,T,RH");
  unsigned long until = vextOnAtMs + (unsigned long)AIR_PROBE_MS;
  while ((long)(millis() - until) < 0) {
    // Без повторів і без reset(): нас цікавить сира поведінка сенсора в часі, а
    // не результат виправлень. NAN тут — теж дані, він показує, з якої саме
    // мілісекунди чіп узагалі починає відповідати.
    float t = sht31.readTemperature();
    float h = sht31.readHumidity();
    LOG("%lu,%.2f,%.2f\n", millis() - vextOnAtMs, t, h);
    delay(AIR_PROBE_EVERY_MS);
  }
}
#endif

bool airRead(float &temp, float &hum) {
  for (int attempt = 0; attempt < SHT_RETRIES; attempt++) {
    temp = sht31.readTemperature();
    hum = sht31.readHumidity();
    if (!isnan(temp) && !isnan(hum)) return true;

    // Повторити те саме звернення до сенсора, який уже мовчить, — марно.
    // Скидання повертає його з підвислого стану, у який він міг потрапити на
    // старті живлення; без цього три спроби були трьома однаковими невдачами.
    sht31.reset();
    delay(SHT_RETRY_DELAY_MS);
  }
  LOG_LN("SHT31 read failed (NAN) after retries");
  logEvent(EV_SHT_NAN);
  return false;
}
