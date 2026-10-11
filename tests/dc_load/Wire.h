#pragma once
#include <cassert>
#include <cstdint>
#include <vector>
#include "../../src/config.h"
struct DcTestSample {
    uint16_t bus = (3000U << 3) | 2U; // 12 V, conversion ready.
    int16_t shunt = 1500, current = 6000; // 15 mV, ~10 A for 50 A/75 mV.
};
inline bool dcPresent = true, dcRegisterOk = true, dcCalibrationOk = true;
inline DcTestSample dcDefault;
inline std::vector<DcTestSample> dcSamples;
inline size_t dcSampleIndex = 0;
inline uint16_t dcRegs[6] = {};
inline int dcWrites = 0, dcOtherAddresses = 0;
struct TwoWire {
    uint8_t reg = 0, sent = 0, byte = 0;
    uint16_t value = 0, received = 0;
    void beginTransmission(uint8_t address) {
        if (address != DC_LOAD_INA219_I2C_ADDRESS) ++dcOtherAddresses;
        assert(address == DC_LOAD_INA219_I2C_ADDRESS); sent = 0; value = 0;
    }
    void write(uint8_t input) {
        if (sent++ == 0) reg = input;
        else value = static_cast<uint16_t>((value << 8) | input);
    }
    int endTransmission(bool = true) {
        if (!dcPresent || !dcRegisterOk) return 2;
        if (sent == 3) { dcRegs[reg] = value; ++dcWrites; }
        return 0;
    }
    int requestFrom(uint8_t address, uint8_t count) {
        assert(address == DC_LOAD_INA219_I2C_ADDRESS && count == 2);
        const auto sample = dcSampleIndex < dcSamples.size() ? dcSamples[dcSampleIndex] : dcDefault;
        received = reg == 1 ? static_cast<uint16_t>(sample.shunt) : reg == 2 ? sample.bus :
            reg == 4 ? static_cast<uint16_t>(sample.current) :
            reg == 5 && !dcCalibrationOk ? 0 : dcRegs[reg];
        byte = 0; return dcPresent && dcRegisterOk ? 2 : 0;
    }
    int read() {
        if (byte++ == 0) return received >> 8;
        if (reg == 4) ++dcSampleIndex;
        return received & 255;
    }
};
inline TwoWire Wire;
