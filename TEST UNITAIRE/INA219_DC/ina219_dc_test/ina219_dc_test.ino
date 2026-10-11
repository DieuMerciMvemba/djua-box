#include <Arduino.h>
#include <Wire.h>
#include "../../../src/config.h"
#include "../../../src/pins.h"
#include "../../../src/sensors/dc_load_power_sensor.h"

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN); Wire.setClock(INA219_I2C_CLOCK_HZ);
    Serial.printf("DC INA219 adresse=0x%02X | shunt exemple %.1f A / %.1f mV\n",
        DC_LOAD_INA219_I2C_ADDRESS, DC_LOAD_SHUNT_RATED_A, DC_LOAD_SHUNT_RATED_MV);
    Serial.println("Verifier le montage/shunt et definir DC_LOAD_SHUNT_CONFIGURED=1 dans src/config.h apres validation.");
    Serial.println(initDcLoadPowerSensor() ? "DC INA219 OK" : "DC INA219 NON CONFIGURE / INDISPONIBLE");
}
void loop() {
    const auto dc = readDcLoadPowerSensor();
    Serial.printf("valid=%u | V=%.3f | A=%.3f | W=%.2f | shunt_mV=%.3f | samples=%u | recovery=%u\n",
        dc.valid, dc.voltage, dc.current, dc.power, dc.shuntVoltageMv, dc.sampleCount, dc.recoveryAttempted);
    delay(1000);
}
