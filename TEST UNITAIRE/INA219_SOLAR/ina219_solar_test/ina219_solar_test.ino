#include <Arduino.h>
#include <Wire.h>
#define IOT_HARDWARE_V2 0
#include "../../../src/pins.h"
// Tester exactement le driver de production, sans copie de son algorithme.
#include "../../../src/sensors/solar_power_sensor.cpp"

void setup()
{
    Serial.begin(SERIAL_BAUD_RATE);
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(INA219_I2C_CLOCK_HZ);
    Serial.println("[SOLAR INA219] Initialisation...");
    if (initSolarPowerSensor())
        Serial.printf("[SOLAR INA219] OK | adresse=0x%02X\n", SOLAR_INA219_I2C_ADDRESS);
    else
        Serial.println("[SOLAR INA219] INDISPONIBLE");
}

void loop()
{
    const SolarPowerData data = readSolarPowerSensor();
    Serial.println("=== INA219 SOLAIRE ===");
    Serial.printf("Adresse : 0x%02X\n", SOLAR_INA219_I2C_ADDRESS);
    Serial.printf("Bus Voltage : %.3f V\n", data.busVoltage);
    Serial.printf("Shunt Voltage : %.3f mV\n", data.shuntVoltageMv);
    Serial.printf("Solar Voltage : %.3f V\n", data.voltage);
    Serial.printf("Solar Current : %.4f A\n", data.current);
    Serial.printf("Solar Power : %.3f W\n", data.power);
    Serial.printf("Samples : %u/%u\n", data.sampleCount, SOLAR_INA219_SAMPLE_COUNT);
    Serial.printf("Recovery attempted : %s\n", data.recoveryAttempted ? "true" : "false");
    Serial.printf("Valid : %s\n", data.valid ? "true" : "false");
    if (!data.valid) Serial.println("Valeurs ci-dessus non exploitables : mesure invalide.");
    delay(2000);
}
