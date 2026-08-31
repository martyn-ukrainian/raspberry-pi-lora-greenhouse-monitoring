#include "telemetry/radio_link.h"

#include <RadioLib.h>
#include <SPI.h>

#include "core/board.h"
#include "core/events.h"
#include "core/log.h"

static SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
static bool radioReady = false;

bool radioIsReady() {
  return radioReady;
}

bool radioBegin() {
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  int state = radio.begin(868.0);
  radioReady = (state == RADIOLIB_ERR_NONE);
  if (!radioReady) {
    LOG("LoRa init failed, code %d\n", state);
    logEvent(EV_LORA_INIT_FAIL);
  }
  return radioReady;
}

bool radioTransmit(String &msg) {
  int state = radio.transmit(msg);
  if (state == RADIOLIB_ERR_NONE) {
    LOG_LN("Sent! " + msg);
    return true;
  }
  LOG("Send failed, code %d\n", state);
  logEvent(EV_LORA_TX_FAIL);
  return false;
}

void radioSleep() {
  int state = radio.sleep(false);
  if (state != RADIOLIB_ERR_NONE && radioReady) {
    LOG("radio.sleep failed, code %d\n", state);
    logEvent(EV_RADIO_SLEEP_FAIL);
  }
}
