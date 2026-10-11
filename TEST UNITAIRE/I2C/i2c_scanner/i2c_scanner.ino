#include <Arduino.h>
#include <Wire.h>
#include "../../../src/config.h"
#include "../../../src/pins.h"

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(V2_I2C_CLOCK_HZ);
    Wire.setTimeOut(50);
    Serial.printf("Attendus : BAT INA228 0x%02X, SOL INA228 0x%02X, INA3221 0x%02X, TV INA228 0x%02X\n",
        V2_BATTERY_ADDRESS, V2_SOLAR_ADDRESS, V2_DC_ADDRESS, V2_TV_ADDRESS);
}
void loop() {
    for (uint8_t address = 1; address < 127; ++address) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() == 0) Serial.printf("Reponse I2C : 0x%02X\n", address);
    }
    Serial.println("Tester chaque module separement : deux modules a la meme adresse ne sont pas distingues.");
    delay(5000);
}
