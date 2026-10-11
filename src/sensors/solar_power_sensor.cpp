#include "../config.h"
#if !IOT_HARDWARE_V2
#include "solar_power_sensor.h"
#include "../config.h"

#if SOLAR_MEASUREMENT_ENABLED
#include <Adafruit_INA219.h>
#include <Wire.h>
#include <math.h>

namespace
{
// Limites du composant et de la calibration 32V_2A avec shunt 0,1 ohm.
// Le nom du calibre 32V ne releve PAS la limite physique de 26 V.
constexpr float MAX_BUS_V = 26.0F;
constexpr float MAX_SHUNT_MV = 320.0F;
constexpr float MAX_CURRENT_MA = 3200.0F;
constexpr uint8_t MINIMUM_VALID_SAMPLES = SOLAR_INA219_SAMPLE_COUNT / 2U + 1U;
static_assert(SOLAR_INA219_SAMPLE_COUNT >= 3U &&
              SOLAR_INA219_SAMPLE_COUNT <= 15U &&
              SOLAR_INA219_SAMPLE_COUNT % 2U == 1U, "Filtre solaire invalide");
static_assert(SOLAR_CURRENT_POLARITY == 1 || SOLAR_CURRENT_POLARITY == -1,
              "La polarite solaire doit etre 1 ou -1");

Adafruit_INA219 solarIna219(SOLAR_INA219_I2C_ADDRESS);
bool solarSensorAvailable = false;

bool readRegister(uint8_t reg, uint16_t& value)
{
    Wire.beginTransmission(SOLAR_INA219_I2C_ADDRESS);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(static_cast<uint8_t>(SOLAR_INA219_I2C_ADDRESS),
                         static_cast<uint8_t>(2)) != 2) return false;
    value = static_cast<uint16_t>(Wire.read()) << 8;
    value |= static_cast<uint8_t>(Wire.read());
    return true;
}

bool configureSolarINA219()
{
    // Ni Wire.end(), ni changement de broches/horloge sur le bus partage.
    if (!solarIna219.begin(&Wire)) return false;
    solarIna219.setCalibration_32V_2A();
    if (!solarIna219.success()) return false;
    // La bibliotheque ne remonte que le resultat de sa derniere operation.
    // Verifier aussi l'ecriture de calibration (4096 pour un shunt 0,1 ohm).
    uint16_t calibration = 0;
    if (!readRegister(INA219_REG_CALIBRATION, calibration) || calibration != 4096U)
        return false;
    delay(SOLAR_INA219_SAMPLE_DELAY_MS);
    return true;
}

float median(float* values, uint8_t count)
{
    for (uint8_t i = 1; i < count; ++i)
    {
        const float value = values[i];
        uint8_t j = i;
        while (j > 0 && values[j - 1] > value)
        {
            values[j] = values[j - 1];
            --j;
        }
        values[j] = value;
    }
    return count % 2U ? values[count / 2U]
        : (values[count / 2U - 1U] + values[count / 2U]) / 2.0F;
}

SolarPowerData readFilteredSolarData()
{
    SolarPowerData data = {};
    float buses[SOLAR_INA219_SAMPLE_COUNT] = {};
    float shunts[SOLAR_INA219_SAMPLE_COUNT] = {};
    float currents[SOLAR_INA219_SAMPLE_COUNT] = {};
    for (uint8_t i = 0; i < SOLAR_INA219_SAMPLE_COUNT; ++i)
    {
        const float shunt = solarIna219.getShuntVoltage_mV();
        const bool shuntOk = solarIna219.success();
        const float bus = solarIna219.getBusVoltage_V();
        const bool busOk = solarIna219.success();
        const float current = solarIna219.getCurrent_mA();
        const bool currentOk = solarIna219.success();
        // getBusVoltage_V() masque OVF : le lire explicitement pour rejeter
        // une saturation qui pourrait sinon ressembler a une production nulle.
        uint16_t busStatus = 0, calibration = 0;
        const bool registersOk = readRegister(INA219_REG_BUSVOLTAGE, busStatus) &&
            readRegister(INA219_REG_CALIBRATION, calibration);
        const float voltage = bus + shunt / 1000.0F;
        if (shuntOk && busOk && currentOk && registersOk &&
            !(busStatus & 1U) && calibration == 4096U &&
            isfinite(bus) && isfinite(shunt) && isfinite(current) &&
            bus >= 0.0F && bus <= MAX_BUS_V &&
            voltage >= 0.0F && voltage <= MAX_BUS_V &&
            shunt > -MAX_SHUNT_MV && shunt < MAX_SHUNT_MV &&
            current > -MAX_CURRENT_MA && current < MAX_CURRENT_MA)
        {
            buses[data.sampleCount] = bus;
            shunts[data.sampleCount] = shunt;
            currents[data.sampleCount++] = current;
        }
        if (i + 1U < SOLAR_INA219_SAMPLE_COUNT)
            delay(SOLAR_INA219_SAMPLE_DELAY_MS);
    }
    if (data.sampleCount < MINIMUM_VALID_SAMPLES) return data;
    data.busVoltage = median(buses, data.sampleCount);
    data.shuntVoltageMv = median(shunts, data.sampleCount);
    data.sensorCurrentMa = median(currents, data.sampleCount);
    data.voltage = data.busVoltage + data.shuntVoltageMv / 1000.0F;
    data.current = data.sensorCurrentMa / 1000.0F * SOLAR_CURRENT_POLARITY;
    data.power = data.voltage * data.current;
    data.valid = isfinite(data.power) && data.voltage >= 0.0F && data.voltage <= MAX_BUS_V;
    return data;
}
} // namespace
#endif

bool initSolarPowerSensor()
{
#if SOLAR_MEASUREMENT_ENABLED
    solarSensorAvailable = configureSolarINA219();
    return solarSensorAvailable;
#else
    return false;
#endif
}

SolarPowerData readSolarPowerSensor()
{
#if SOLAR_MEASUREMENT_ENABLED
    bool recoveryAttempted = false;
    if (!solarSensorAvailable)
    {
        recoveryAttempted = true;
        solarSensorAvailable = configureSolarINA219();
        if (!solarSensorAvailable)
        {
            SolarPowerData data = {};
            data.recoveryAttempted = true;
            return data;
        }
    }
    SolarPowerData data = readFilteredSolarData();
    if (!data.valid && !recoveryAttempted)
    {
        // Une seule reconfiguration par cycle ; aucune boucle de retry infinie.
        recoveryAttempted = true;
        Serial.println("[SOLAR INA219] Lecture invalide, tentative de recuperation...");
        solarSensorAvailable = configureSolarINA219();
        if (solarSensorAvailable) data = readFilteredSolarData();
    }
    data.recoveryAttempted = recoveryAttempted;
    return data;
#else
    return SolarPowerData{};
#endif
}

bool isSolarPowerSensorAvailable()
{
#if SOLAR_MEASUREMENT_ENABLED
    return solarSensorAvailable;
#else
    return false;
#endif
}

#endif // legacy sensor, not linked into target V2
