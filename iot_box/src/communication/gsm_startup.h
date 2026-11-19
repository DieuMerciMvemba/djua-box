#pragma once
#include <stdint.h>
namespace djua {
// One initial PWRKEY pulse, then AT retries only. Network failures must not
// toggle PWRKEY blindly (which could switch off an already running module).
class GsmStartup {
public:
    GsmStartup(uint32_t stabilize = 1000, uint32_t pulse = 1200, uint32_t boot = 5000)
        : stabilize_(stabilize), pulse_(pulse), boot_(boot) {}
    void begin(bool usePwrkey, uint32_t now) {
        usePwrkey_ = usePwrkey; phase_ = usePwrkey ? Stabilizing : Booting; since_ = now;
    }
    void update(uint32_t now) {
        if (phase_ == Stabilizing && now - since_ >= stabilize_) { phase_ = Pulsing; since_ = now; }
        else if (phase_ == Pulsing && now - since_ >= pulse_) { phase_ = Booting; since_ = now; }
        else if (phase_ == Booting && now - since_ >= boot_) phase_ = Ready;
    }
    bool asserted() const { return usePwrkey_ && phase_ == Pulsing; }
    bool ready() const { return phase_ == Ready; }
private:
    enum Phase { Stabilizing, Pulsing, Booting, Ready };
    Phase phase_ = Booting;
    bool usePwrkey_ = false;
    uint32_t since_ = 0, stabilize_, pulse_, boot_;
};
}
