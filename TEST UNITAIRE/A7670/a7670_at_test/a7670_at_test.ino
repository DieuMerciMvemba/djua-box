#include <Arduino.h>
#include "../../../src/config.h"
#include "../../../src/pins.h"
// Manual AT bench; no automatic SMS, provisioning, NVS erase or modem GNSS.
static HardwareSerial modemUart(1);
void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
#if GSM_HARDWARE_CONFIGURED
    modemUart.setRxBufferSize(512);
    modemUart.begin(GSM_BAUD_RATE, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);
    Serial.println("[A7670] UART1 ouvert ; envoyer les commandes AT avec fin de ligne CR.");
#else
    Serial.println("[A7670] Confirmer alimentation/niveaux et GSM_HARDWARE_CONFIGURED avant utilisation.");
#endif
}
void loop() {
#if GSM_HARDWARE_CONFIGURED
    for (unsigned i = 0; i < 128 && modemUart.available(); ++i) Serial.write(modemUart.read());
    for (unsigned i = 0; i < 128 && Serial.available(); ++i) modemUart.write(Serial.read());
#endif
}
