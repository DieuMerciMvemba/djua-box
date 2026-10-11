#include "../config.h"
#if !IOT_HARDWARE_V2
#include "power_sensor.h"

#include <Adafruit_INA219.h>
#include <Wire.h>
#include <math.h>

#include "../config.h"
#include "../pins.h"

namespace
{
constexpr float INA219_MAX_BUS_V = 32.0F;
constexpr float INA219_MAX_SHUNT_MV = 320.0F;
constexpr float INA219_MAX_CURRENT_MA = 3200.0F;
constexpr uint8_t MINIMUM_VALID_SAMPLES =
    static_cast<uint8_t>((INA219_SAMPLE_COUNT / 2U) + 1U);

static_assert(INA219_SAMPLE_COUNT >= 3U,
              "Le filtre INA219 exige au moins trois mesures.");
static_assert((INA219_SAMPLE_COUNT % 2U) == 1U,
              "Le nombre de mesures INA219 doit etre impair.");
static_assert(INA219_SAMPLE_COUNT <= 15U,
              "Le tampon INA219 est volontairement limite a 15 mesures.");
static_assert(BATTERY_MIN_VALID_V > 0.0F &&
                  BATTERY_MAX_VALID_V > BATTERY_MIN_VALID_V,
              "La plage de tension batterie est invalide.");

Adafruit_INA219 ina219(INA219_I2C_ADDRESS);
bool powerSensorAvailable = false;

struct SensorSample
{
    float busVoltageV;
    float shuntVoltageMv;
    float currentMa;
    bool valid;
};

bool configureINA219()
{
    Wire.setClock(INA219_I2C_CLOCK_HZ);
    if (!ina219.begin(&Wire))
    {
        return false;
    }

    ina219.setCalibration_32V_2A();
    delay(INA219_SAMPLE_DELAY_MS);
    return true;
}

SensorSample readSensorSample()
{
    SensorSample sample = {};

    sample.shuntVoltageMv = ina219.getShuntVoltage_mV();
    const bool shuntReadOk = ina219.success();

    sample.busVoltageV = ina219.getBusVoltage_V();
    const bool busReadOk = ina219.success();

    sample.currentMa = ina219.getCurrent_mA();
    const bool currentReadOk = ina219.success();

    sample.valid =
        shuntReadOk && busReadOk && currentReadOk &&
        isfinite(sample.shuntVoltageMv) &&
        isfinite(sample.busVoltageV) &&
        isfinite(sample.currentMa);
    return sample;
}

void sortValues(float* values, uint8_t count)
{
    for (uint8_t index = 1U; index < count; ++index)
    {
        const float value = values[index];
        uint8_t position = index;
        while (position > 0U && values[position - 1U] > value)
        {
            values[position] = values[position - 1U];
            --position;
        }
        values[position] = value;
    }
}

float median(float* values, uint8_t count)
{
    sortValues(values, count);
    return values[count / 2U];
}

PowerData readFilteredData()
{
    PowerData data = {};
    float busVoltages[INA219_SAMPLE_COUNT] = {};
    float shuntVoltages[INA219_SAMPLE_COUNT] = {};
    float currents[INA219_SAMPLE_COUNT] = {};
    uint8_t validSampleCount = 0U;

    for (uint8_t attempt = 0U; attempt < INA219_SAMPLE_COUNT; ++attempt)
    {
        const SensorSample sample = readSensorSample();
        if (sample.valid)
        {
            busVoltages[validSampleCount] = sample.busVoltageV;
            shuntVoltages[validSampleCount] = sample.shuntVoltageMv;
            currents[validSampleCount] = sample.currentMa;
            validSampleCount++;
        }

        if (attempt + 1U < INA219_SAMPLE_COUNT)
        {
            delay(INA219_SAMPLE_DELAY_MS);
        }
    }

    data.sampleCount = validSampleCount;
    if (validSampleCount < MINIMUM_VALID_SAMPLES)
    {
        return data;
    }

    data.busVoltage = median(busVoltages, validSampleCount);
    data.shuntVoltageMv = median(shuntVoltages, validSampleCount);
    data.sensorCurrentMa = median(currents, validSampleCount);
    data.voltage = data.busVoltage + (data.shuntVoltageMv / 1000.0F);
    data.current = data.sensorCurrentMa / 1000.0F * BATTERY_CURRENT_POLARITY;
    data.power = data.voltage * data.current;

    const bool electricalRangeValid =
        data.busVoltage >= 0.0F &&
        data.busVoltage <= INA219_MAX_BUS_V &&
        fabsf(data.shuntVoltageMv) <= INA219_MAX_SHUNT_MV &&
        fabsf(data.sensorCurrentMa) <= INA219_MAX_CURRENT_MA;
    const bool batteryRangeValid =
        data.voltage >= BATTERY_MIN_VALID_V &&
        data.voltage <= BATTERY_MAX_VALID_V;

    data.valid = electricalRangeValid && batteryRangeValid;
    return data;
}
} // namespace

bool initPowerSensor()
{
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(INA219_I2C_CLOCK_HZ);
    powerSensorAvailable = configureINA219();
    return powerSensorAvailable;
}

PowerData readPowerSensor()
{
    bool recoveryAttempted = false;

    if (!powerSensorAvailable)
    {
        recoveryAttempted = true;
        powerSensorAvailable = configureINA219();
        if (!powerSensorAvailable)
        {
            PowerData unavailable = {};
            unavailable.recoveryAttempted = true;
            return unavailable;
        }
    }

    PowerData data = readFilteredData();
    if (!data.valid)
    {
        recoveryAttempted = true;
        Serial.println(
            "[INA219] Lecture incoherente, tentative de reinitialisation I2C..."
        );
        powerSensorAvailable = configureINA219();
        if (powerSensorAvailable)
        {
            data = readFilteredData();
        }
    }

    data.recoveryAttempted = recoveryAttempted;
    return data;
}

bool isPowerSensorAvailable()
{
    return powerSensorAvailable;
}

#endif // legacy sensor, not linked into target V2
