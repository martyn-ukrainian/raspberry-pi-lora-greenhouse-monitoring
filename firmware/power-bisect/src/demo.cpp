// Демонстраційний цикл: видно очима, що відбувається з платою.
//
// Кроки бісекції (`main.cpp`) навмисне мовчазні — вони засинають назавжди, і
// на столі плата виглядає мертвою. Для заміру це правильно, для розуміння —
// ні: не відрізнити «спить як задумано» від «зависла на старті», і кожен
// стрибок приладу доводиться вгадувати.
//
// Тут навпаки: короткий цикл, екран показує фазу й відлік, і на приладі видно,
// як число ходить між неспанням і сном у такт із написами.
//
// Два режими, різниця рівно в тому, ЯК плата лягає спати:
//
//   DEMO_CLEAN  повний розбір перед сном (рецепт ropg): холодний сон радіо,
//               Vext знято, усі периферійні піни в INPUT.  -> ~0,01 мА
//   DEMO_LEAKY  так, як робить бойова прошивка: Vext знято, радіо в теплий
//               сон, піни лишаються виходами.              -> ~0,84 мА
//
// Усе інше в обох режимах однакове — той самий екран, той самий цикл. Тому
// різниця на приладі є ціною саме розбору, а не чогось іще.

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include <SSD1306Wire.h>
#include <Wire.h>

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

#define LED_PIN    35
#define VBAT_CTRL  37
#define VBAT_ADC   1

// Скільки секунд плата не спить (з відліком на екрані) і скільки спить.
//
// Сон 10 с, а не 5: на п'яти секундах плата встигає прокинутись раніше, ніж
// мультиметр устигне показати усталене число, і виглядає це так, ніби вона
// вмикається одразу. Десять секунд дають приладу час зупинитись.
#ifndef AWAKE_S
#define AWAKE_S 3
#endif
#ifndef SLEEP_S
#define SLEEP_S 10
#endif

SSD1306Wire display(0x3c, OLED_SDA, OLED_SCL);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
TwoWire airWire = TwoWire(1);

// Переживає deep sleep: RTC-пам'ять не гасне разом з основною. Без лічильника
// не відрізнити новий цикл від зависання на старому.
RTC_DATA_ATTR uint32_t bootCount = 0;

#ifdef DEMO_CLEAN
#define MODE_NAME "CLEAN (pins INPUT)"
#else
#define MODE_NAME "LEAKY (as production)"
#endif

void drawFrame(const String &big, const String &note) {
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "cycle #" + String(bootCount));
  display.drawString(0, 12, MODE_NAME);
  display.setFont(ArialMT_Plain_24);
  display.drawString(0, 28, big);
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 54, note);
  display.display();
}

void setup() {
  bootCount++;

  // Vext живить екран: LOW = живлення подано.
  pinMode(VEXT_CTRL, OUTPUT);
  digitalWrite(VEXT_CTRL, LOW);
  delay(50);

  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(20);
  digitalWrite(OLED_RST, HIGH);

  display.init();
  display.flipScreenVertically();

  // Периферія піднімається так само, як у бойовій прошивці — інакше в режимі
  // LEAKY нічого було б і витікати: піни стають виходами саме тут.
  pinMode(AIR_SDA, INPUT_PULLUP);
  pinMode(AIR_SCL, INPUT_PULLUP);
  airWire.begin(AIR_SDA, AIR_SCL);

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  radio.begin(868.0);

  // Неспання з відліком. Прилад у цій фазі показує ~40-50 мА: процесор плюс
  // екран.
  for (int n = AWAKE_S; n > 0; n--) {
    drawFrame(String(n), "awake -> sleep in " + String(n) + "s");
    delay(1000);
  }

  drawFrame("zZ", "sleeping " + String(SLEEP_S) + "s");
  delay(400);
  display.displayOff();

#ifdef DEMO_CLEAN
  // Холодний сон радіо: конфігурація не зберігається, зате 160 нА замість
  // 0,6-1,2 мкА. Втрата безкоштовна — після deep sleep радіо все одно
  // налаштовується заново.
  radio.sleep(false);
  digitalWrite(VEXT_CTRL, HIGH);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Те, чого бойова прошивка не робить: жоден пін не лишається виходом.
  const int idlePins[] = {VBAT_CTRL, VBAT_ADC, LED_PIN,
                          LORA_DIO1, LORA_RST, LORA_BUSY, LORA_NSS,
                          LORA_MISO, LORA_MOSI, LORA_SCK,
                          OLED_SDA, OLED_SCL, OLED_RST,
                          AIR_SDA, AIR_SCL};
  for (unsigned i = 0; i < sizeof(idlePins) / sizeof(idlePins[0]); i++) {
    pinMode(idlePins[i], INPUT);
  }
#else
  // Рівно те, що робить бойова прошивка сьогодні: теплий сон радіо, зняти
  // Vext, і по всьому. Піни лишаються там, де їх залишили SPI і Wire.
  radio.sleep();
  digitalWrite(VEXT_CTRL, HIGH);
#endif

  esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_S * 1000000ULL);
  esp_deep_sleep_start();
}

void loop() {}
