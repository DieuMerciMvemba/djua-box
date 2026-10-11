#include <Arduino.h>
#include <Wire.h>
#include "../../../src/config.h"
#include "../../../src/pins.h"

void setup()
{
    Serial.begin(SERIAL_BAUD_RATE);
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(INA219_I2C_CLOCK_HZ);
}

void loop()
{
    bool batteryFound = false, solarFound = false, dcFound = false;
    Serial.println("=== SCANNER I2C : SDA21 / SCL22 ===");
    for (uint8_t address = 1; address < 127; ++address)
    {
        Wire.beginTransmission(address);
        const uint8_t error = Wire.endTransmission();
        if (error == 0)
        {
            Serial.printf("0x%02X\n", address);
            batteryFound |= address == INA219_I2C_ADDRESS;
            solarFound |= address == SOLAR_INA219_I2C_ADDRESS;
            dcFound |= address == DC_LOAD_INA219_I2C_ADDRESS;
        }
        else if (error != 2)
            Serial.printf("Erreur I2C %u a 0x%02X\n", error, address);
    }
    Serial.printf("Batterie attendue 0x%02X : %s\n", INA219_I2C_ADDRESS,
                  batteryFound ? "ACK" : "ABSENTE");
    Serial.printf("Solaire attendu 0x%02X : %s\n", SOLAR_INA219_I2C_ADDRESS,
                  solarFound ? "ACK" : "ABSENT - NON OPERATIONNEL");
    Serial.printf("Charge DC attendue 0x%02X : %s\n", DC_LOAD_INA219_I2C_ADDRESS,
                  dcFound ? "ACK" : "ABSENTE - ADRESSE A CONFIGURER");
    Serial.println("ACK confirme une adresse, pas l'identite ni le cablage VIN du capteur.");
    delay(3000);
}
