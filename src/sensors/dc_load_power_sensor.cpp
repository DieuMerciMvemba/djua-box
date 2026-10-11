#include "../config.h"
#if !IOT_HARDWARE_V2
#include "dc_load_power_sensor.h"
#include "dc_load_calibration.h"
#include "../config.h"
#if DC_LOAD_MEASUREMENT_ENABLED && DC_LOAD_SHUNT_CONFIGURED
#include <Wire.h>
namespace {
constexpr uint16_t CONFIG = 0x399F; // bus32V, shunt +/-320mV, ADC12bit, continu.
static_assert(DC_LOAD_INA219_SAMPLE_COUNT >= 3 && DC_LOAD_INA219_SAMPLE_COUNT <= 15 &&
              DC_LOAD_INA219_SAMPLE_COUNT % 2 == 1, "Filtre DC invalide");
DcLoadCalibration calibration;
bool available = false;
bool readRegister(uint8_t reg, uint16_t& value) {
    Wire.beginTransmission(DC_LOAD_INA219_I2C_ADDRESS); Wire.write(reg);
    if (Wire.endTransmission(false) != 0 ||
        Wire.requestFrom(static_cast<uint8_t>(DC_LOAD_INA219_I2C_ADDRESS), static_cast<uint8_t>(2)) != 2)
        return false;
    value = static_cast<uint16_t>(Wire.read()) << 8;
    value |= static_cast<uint8_t>(Wire.read()); return true;
}
bool writeRegister(uint8_t reg, uint16_t value) {
    Wire.beginTransmission(DC_LOAD_INA219_I2C_ADDRESS);
    Wire.write(reg); Wire.write(static_cast<uint8_t>(value >> 8)); Wire.write(static_cast<uint8_t>(value));
    return Wire.endTransmission() == 0;
}
bool configure() {
    if (!makeDcLoadCalibration(DC_LOAD_SHUNT_RATED_A, DC_LOAD_SHUNT_RATED_MV, calibration)) return false;
    if (!writeRegister(0, CONFIG) || !writeRegister(5, calibration.reg)) return false;
    uint16_t config = 0, cal = 0;
    if (!readRegister(0, config) || !readRegister(5, cal) || config != CONFIG || cal != calibration.reg)
        return false;
    delay(DC_LOAD_INA219_SAMPLE_DELAY_MS); return true;
}
int32_t signed16(uint16_t raw) { return raw & 0x8000U ? static_cast<int32_t>(raw) - 65536 : raw; }
float median(float* values, uint8_t n) {
    for (uint8_t i = 1; i < n; ++i) {
        const float v = values[i]; uint8_t j = i;
        while (j && values[j - 1] > v) { values[j] = values[j - 1]; --j; }
        values[j] = v;
    }
    return n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2;
}
DcLoadPowerData filtered() {
    DcLoadPowerData data;
    float buses[DC_LOAD_INA219_SAMPLE_COUNT], shunts[DC_LOAD_INA219_SAMPLE_COUNT], currents[DC_LOAD_INA219_SAMPLE_COUNT];
    for (uint8_t n = 0; n < DC_LOAD_INA219_SAMPLE_COUNT; ++n) {
        uint16_t bus = 0, shunt = 0, current = 0, cal = 0, config = 0;
        const bool ok = readRegister(1, shunt) && readRegister(2, bus) &&
            readRegister(4, current) && readRegister(5, cal) && readRegister(0, config);
        const float voltage = (bus >> 3) * 0.004F; // VIN- : tension cote charge.
        const float shuntMv = signed16(shunt) * 0.01F;
        const float amps = static_cast<float>(signed16(current) * calibration.currentLsbA);
        const float sourceV = voltage + shuntMv / 1000.0F;
        if (ok && !(bus & 1U) && (bus & 2U) && cal == calibration.reg && config == CONFIG &&
            voltage <= DC_LOAD_MAX_VALID_V && sourceV >= 0 && sourceV <= 26 &&
            fabsf(shuntMv) < 320 && fabsf(shuntMv) <= DC_LOAD_SHUNT_RATED_MV &&
            fabsf(amps) <= DC_LOAD_SHUNT_RATED_A && signed16(current) > -32768 && signed16(current) < 32767) {
            buses[data.sampleCount] = voltage; shunts[data.sampleCount] = shuntMv;
            currents[data.sampleCount++] = amps;
        }
        if (n + 1 < DC_LOAD_INA219_SAMPLE_COUNT) delay(DC_LOAD_INA219_SAMPLE_DELAY_MS);
    }
    if (data.sampleCount < DC_LOAD_INA219_SAMPLE_COUNT / 2 + 1) return data;
    data.voltage = data.busVoltage = median(buses, data.sampleCount);
    data.shuntVoltageMv = median(shunts, data.sampleCount);
    data.sensorCurrentMa = median(currents, data.sampleCount) * 1000;
    data.current = data.sensorCurrentMa / 1000 * DC_LOAD_CURRENT_POLARITY;
    data.power = data.voltage * data.current;
    data.valid = isfinite(data.power); return data;
}
}
#endif
bool initDcLoadPowerSensor() {
#if DC_LOAD_MEASUREMENT_ENABLED && DC_LOAD_SHUNT_CONFIGURED
    available = configure(); return available;
#else
    return false;
#endif
}
DcLoadPowerData readDcLoadPowerSensor() {
#if DC_LOAD_MEASUREMENT_ENABLED && DC_LOAD_SHUNT_CONFIGURED
    bool recovered = false;
    if (!available) {
        recovered = true; available = configure();
        if (!available) { DcLoadPowerData data; data.recoveryAttempted = true; return data; }
    }
    auto data = filtered();
    if (!data.valid && !recovered) {
        recovered = true; Serial.println("[DC INA219] Lecture invalide, reconfiguration du capteur");
        available = configure(); if (available) data = filtered();
    }
    data.recoveryAttempted = recovered; return data;
#else
    return DcLoadPowerData{};
#endif
}
bool isDcLoadPowerSensorAvailable() {
#if DC_LOAD_MEASUREMENT_ENABLED && DC_LOAD_SHUNT_CONFIGURED
    return available;
#else
    return false;
#endif
}

#endif // legacy sensor, not linked into target V2
