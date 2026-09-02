#include "sensors/battery.h"

#include <Arduino.h>

#include "core/board.h"
#include "core/events.h"

// Плата гасне вже на ~3,4 В на банці (лінійний регулятор — див.
// docs/дослідження/power-budget.md).
//
// УВАГА: VBAT_CRITICAL_V стоїть НИЖЧЕ за точку смерті плати, тобто ця подія,
// найімовірніше, не спрацює ніколи — вузол помре, не встигнувши її надіслати.
// Число лишається як у v1 навмисно: точка смерті поки оцінена, а не заміряна, і
// правити його треба після того, як батарея сяде й дасть заміряну межу, а не
// підставляти ще одне вгадане. Деталі — docs/дослідження/струм-сну.md.
#define VBAT_LOW_V      3.50f
#define VBAT_CRITICAL_V 3.35f

float batteryVolts() {
  // Дільник вмикається ВИСОКИМ рівнем — протилежно до Vext_Ctrl, і саме за
  // аналогією з ним тут спершу стояв LOW. Наслідок був тихий: vbat завжди
  // читався нулем, тобто прошивка мовчки вважала, що батареї нема.
  pinMode(VBAT_CTRL, OUTPUT);
  digitalWrite(VBAT_CTRL, HIGH);
  delay(10);

  analogSetPinAttenuation(VBAT_ADC, ADC_11db);

  // ЧИ ЦЕ ВЗАГАЛІ БАТАРЕЯ.
  //
  // Поріг за рівнем не працює: на USB БЕЗ комірки дільник читає шину зарядника,
  // тобто цілком правдоподібні 3,7-4,1 В. Саме так цей вузол рапортував vbat
  // 4,02 і 4,25, стоячи на кабелі без жодної банки — і саме на це число потім
  // спирались оцінки ресурсу, поки не з'ясувалось, що воно могло бути не про
  // батарею взагалі.
  //
  // Розрізняє їх СТАБІЛЬНІСТЬ, а не рівень: шина зарядника імпульсна й гуляє на
  // сотні мілівольт між замірами, а справжня комірка стоїть рівно.
  uint32_t lo = 4095, hi = 0;
  for (int i = 0; i < 8; i++) {
    uint32_t one = analogReadMilliVolts(VBAT_ADC);
    if (one < lo) lo = one;
    if (one > hi) hi = one;
    delay(3);
  }
  const bool unstable = (hi - lo) > 40;  // 40 мВ на дільнику = ~200 мВ на комірці

  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) {
    mv += analogReadMilliVolts(VBAT_ADC);
  }
  mv /= 8;

  digitalWrite(VBAT_CTRL, LOW);  // дільник назад у вимкнений стан

  float volts = (mv * VBAT_DIVIDER) / 1000.0f;

  if (unstable || volts < 2.5f) {
    return NAN;
  }

  if (volts < VBAT_CRITICAL_V) {
    logEvent(EV_VBAT_CRITICAL);
  } else if (volts < VBAT_LOW_V) {
    logEvent(EV_VBAT_LOW);
  }
  return volts;
}
