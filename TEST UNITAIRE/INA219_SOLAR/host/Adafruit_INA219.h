#pragma once
#include "Wire.h"
#include <vector>
#define INA219_REG_CALIBRATION 5
#define INA219_REG_BUSVOLTAGE 2
struct TestSample {
    float bus = 18.0F, shunt = 100.0F, current = 1000.0F;
    bool shuntOk = true, busOk = true, currentOk = true;
};
inline std::vector<TestSample> testSamples;
inline TestSample defaultSample;
inline size_t sampleIndex = 0;
inline int sensorBegins = 0;
class Adafruit_INA219 {
    bool ok = true;
    TestSample sample() { return sampleIndex < testSamples.size()
        ? testSamples[sampleIndex] : defaultSample; }
public:
    explicit Adafruit_INA219(uint8_t address) { assert(address == 0x41); }
    bool begin(TwoWire* wire) {
        assert(wire == &Wire); ++sensorBegins; return testPresent;
    }
    void setCalibration_32V_2A() { ok = testPresent; }
    bool success() { return ok; }
    float getShuntVoltage_mV() { ok = testPresent && sample().shuntOk; return sample().shunt; }
    float getBusVoltage_V() { ok = testPresent && sample().busOk; return sample().bus; }
    float getCurrent_mA() {
        const auto value = sample(); ++sampleIndex;
        ok = testPresent && value.currentOk; return value.current;
    }
};
