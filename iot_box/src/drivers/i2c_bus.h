#pragma once
#include <stddef.h>
#include <stdint.h>

namespace djua {
class I2cPort {
public:
    virtual ~I2cPort() = default;
    // Complete transaction, repeated START between pointer write and read.
    virtual bool transfer(uint8_t address, const uint8_t* tx, size_t txSize,
                          uint8_t* rx, size_t rxSize) = 0;
};
class I2cBus {
public:
    explicit I2cBus(I2cPort& port) : port_(port) {}
    bool claim(uint8_t address, const void* owner) {
        if (address < 8 || address > 0x77 || !owner) return false;
        for (size_t i = 0; i < count_; ++i)
            if (addresses_[i] == address) return owners_[i] == owner;
        if (count_ == 16) return false;
        addresses_[count_] = address; owners_[count_++] = owner; return true;
    }
    bool read(uint8_t address, uint8_t reg, uint32_t& value, size_t bytes = 2) {
        if (bytes < 1 || bytes > 4) return false;
        uint8_t rx[4] = {};
        if (!port_.transfer(address, &reg, 1, rx, bytes)) return false;
        value = 0;
        for (size_t i = 0; i < bytes; ++i) value = (value << 8) | rx[i];
        return true;
    }
    bool write(uint8_t address, uint8_t reg, uint16_t value) {
        const uint8_t tx[] = {reg, static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
        return port_.transfer(address, tx, sizeof(tx), nullptr, 0);
    }
private:
    I2cPort& port_;
    uint8_t addresses_[16] = {};
    const void* owners_[16] = {};
    size_t count_ = 0;
};
inline int32_t signedBits(uint32_t raw, unsigned bits) {
    return (raw & (1UL << (bits - 1)))
        ? static_cast<int32_t>(static_cast<int64_t>(raw) - (INT64_C(1) << bits))
        : static_cast<int32_t>(raw);
}
}
