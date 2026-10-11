#pragma once
#include <cassert>
#include <cstdint>
inline bool testPresent = true;
inline bool testOverflow = false;
inline bool testCalibrationOk = true;
inline bool testRegisterOk = true;
inline int wireBegins = 0;
struct TwoWire {
    uint8_t reg = 0;
    int byte = 0;
    void begin() { ++wireBegins; }
    void beginTransmission(uint8_t address) { assert(address == 0x41); }
    void write(uint8_t value) { reg = value; }
    int endTransmission(bool) { return testPresent && testRegisterOk ? 0 : 2; }
    int requestFrom(uint8_t address, uint8_t count) {
        assert(address == 0x41 && count == 2); byte = 0; return 2;
    }
    int read() {
        const uint16_t value = reg == 5 ? (testCalibrationOk ? 4096 : 0)
                                      : (testOverflow ? 1 : 0);
        return byte++ == 0 ? value >> 8 : value & 255;
    }
};
inline TwoWire Wire;
