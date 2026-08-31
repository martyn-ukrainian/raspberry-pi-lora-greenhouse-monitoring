#pragma once

#include <Arduino.h>

#include "sensors/soil.h"

// Формат пакета — той самий JSON, що й у v1: шлюз парсить його як є.
//
// НОВІ ПОЛЯ ГИНУТЬ МОВЧКИ, доки їх не оголосили на приймальному боці. Це вже
// траплялось двічі: `vbat` губився в `GatewayPacket`, потім те саме сталось із
// `soil_at_ms` — поле їхало з коміту 1359c02 і не доїхало до бази ЖОДНОГО разу,
// бо його не було ні в моделі адаптера, ні в тілі запиту, ні в колонці.
// pydantic зайві ключі просто відкидає, без попередження.
//
// Тому `soil_min` / `soil_max` / `soil_n` не вважаються зробленими, доки не
// перевірено, що вони лягли в базу. Порядок правок — у README, «Що треба на
// сервері».
String buildPacket(const SoilBurst &soil, int soilPercent,
                   bool airOk, float airTemp, float airHum, float vbat);
