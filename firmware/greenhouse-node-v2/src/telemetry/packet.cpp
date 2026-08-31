#include "telemetry/packet.h"

#include "config.h"
#include "core/events.h"

String buildPacket(const SoilBurst &soil, int soilPercent,
                   bool airOk, float airTemp, float airHum, float vbat) {
  String json = "{\"type\":\"measurement\",\"node_id\":" + String(NODE_ID);

  // Немає полів = немає даних. Приймальний бік відрізнить це від нуля.
  if (airOk) {
    json += ",\"air_temperature\":" + String(airTemp, 1);
    json += ",\"air_humidity\":" + String(airHum, 1);
  }

  json += ",\"soil_moisture\":" + String(soilPercent);

  // Сире значення поруч із відсотком навмисно: відсоток — це ТЛУМАЧЕННЯ, яке
  // залежить від калібрування, а сире число — сам ВИМІР. Поки робочий діапазон
  // грядки уточнюється, шкала ще зміниться, і тоді історію можна перерахувати
  // без перепрошивки вузлів у полі.
  json += ",\"soil_raw\":" + String(soil.median);

  // Розкид бурста. Заради нього бурст і існує: один відлік нема з чим
  // порівняти, а min/max одразу показують, чи це вимір, чи наводка.
  json += ",\"soil_min\":" + String(soil.minimum);
  json += ",\"soil_max\":" + String(soil.maximum);
  json += ",\"soil_n\":" + String(soil.count);

  // Коли саме знято перший зарахований відлік. Поки це поле їде, прогрів не
  // ховається за мовчазною константою — межу можна підібрати з поля.
  json += ",\"soil_at_ms\":" + String(soil.firstAtMs);

  if (!isnan(vbat)) {
    json += ",\"vbat\":" + String(vbat, 2);
  }
  json += ",\"boot\":" + String(rtcLog.bootCount);

  // err/eseq додаються ТІЛЬКИ коли є що сказати: у здоровому циклі пакет не
  // росте ні на байт.
  if (rtcLog.errFlags != 0) {
    json += ",\"err\":" + String(rtcLog.errFlags);
    json += ",\"eseq\":" + String(rtcLog.errSeq);
  }

  json += "}";
  return json;
}
