#pragma once
#include "d2_store.h"
#include "gsm.h"

namespace djua {
class SmsScheduler {
public:
    SmsScheduler(D2Journal& journal, GsmEngine& modem, uint32_t retryBase = 30000, uint32_t retryMax = 900000)
        : journal_(journal), modem_(modem), retryBase_(retryBase), retryMax_(retryMax) {}
    void update(uint32_t now);
    GsmResult takeResult() { auto result = result_; result_ = GsmResult::None; return result; }
    static uint32_t retryDelay(uint32_t attempts, uint32_t base = 30000, uint32_t maximum = 900000);
private:
    D2Journal& journal_;
    GsmEngine& modem_;
    int active_ = -1;
    uint32_t failedSequence_[D2_QUEUE_CAPACITY] = {}, failedAt_[D2_QUEUE_CAPACITY] = {};
    GsmResult result_ = GsmResult::None;
    uint32_t retryBase_, retryMax_;
};
}
