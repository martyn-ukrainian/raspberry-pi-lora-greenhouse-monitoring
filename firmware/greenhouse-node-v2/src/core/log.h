#pragma once

#include <Arduino.h>

// У теплиці USB не підключений, а кожен printf — це і місце у flash, і час на
// UART, і блокуючий flush перед сном. Тому в бойових збірках Serial мовчить
// повністю; -DAGRO_DEBUG_SERIAL вмикає друк назад, не редагуючи код.
//
// УВАГА: у вимкненому вигляді аргументи LOG() НЕ ОБЧИСЛЮЮТЬСЯ. Тому туди
// не можна класти нічого, крім читання вже готових значень — виклик функції
// всередині LOG() тихо зникне разом із її побічним ефектом.

#ifdef AGRO_DEBUG_SERIAL
#define LOG_BEGIN() Serial.begin(115200)
#define LOG(...)    Serial.printf(__VA_ARGS__)
#define LOG_LN(x)   Serial.println(x)
#define LOG_FLUSH() Serial.flush()
#else
#define LOG_BEGIN() ((void)0)
#define LOG(...)    ((void)0)
#define LOG_LN(x)   ((void)0)
#define LOG_FLUSH() ((void)0)
#endif
