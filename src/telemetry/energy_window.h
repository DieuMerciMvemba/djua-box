#pragma once
#include <stdint.h>
#include <math.h>

namespace djua {
// Les temps sont uint32 (millis) : soustraction modulaire, y compris au wrap.
class EnergyWindow {
public:
    void sample(uint32_t now, double power, bool valid, uint32_t maxGap = 20000) {
        valid = valid && isfinite(power);
        if (!started_) { reset(now, power, valid); return; }
        const uint32_t dt = now - last_;
        if (!valid || !lastValid_ || dt == 0 || dt > maxGap) complete_ = false;
        else energy_ += (lastPower_ + power) * 0.5 * dt / 3600000.0;
        last_ = now; lastPower_ = power; lastValid_ = valid;
    }
    // Appeler apres sample(frontiere), puis reset avec la meme acquisition.
    void reset(uint32_t now, double power, bool valid) {
        started_ = true; start_ = last_ = now; energy_ = 0;
        lastPower_ = power; lastValid_ = complete_ = valid && isfinite(power);
    }
    bool started() const { return started_; }
    bool complete(uint32_t now, uint32_t maxAge = 20000) const {
        return started_ && complete_ && now - last_ <= maxAge;
    }
    double wattHours() const { return energy_; }
    uint32_t durationMs(uint32_t now) const { return now - start_; }
    uint32_t durationSeconds(uint32_t now) const { return (now - start_ + 500U) / 1000U; }
private:
    bool started_ = false, complete_ = false, lastValid_ = false;
    uint32_t start_ = 0, last_ = 0;
    double energy_ = 0, lastPower_ = 0;
};
}
