#include "sms_scheduler.h"

namespace djua {
uint32_t SmsScheduler::retryDelay(uint32_t attempts, uint32_t base, uint32_t maximum) {
    uint32_t delay = base;
    for (uint32_t i = 1; i < attempts && delay < maximum; ++i) delay = delay > maximum / 2 ? maximum : delay * 2;
    return delay;
}
void SmsScheduler::update(uint32_t now) {
    modem_.update(now);
    const GsmResult completed = modem_.takeResult();
    if (active_ >= 0 && completed != GsmResult::None) {
        const auto* item = journal_.at(static_cast<size_t>(active_));
        result_ = completed;
        if (item && completed == GsmResult::AcceptedByModem) {
            for (size_t slot = 0; slot < D2_QUEUE_CAPACITY; ++slot)
                if (failedSequence_[slot] == item->sequence) failedSequence_[slot] = 0;
            if (!journal_.acceptedByModem(static_cast<size_t>(active_))) result_ = GsmResult::Failed;
        } else if (item) {
            size_t slot = 0;
            while (slot < D2_QUEUE_CAPACITY && failedSequence_[slot] && failedSequence_[slot] != item->sequence) ++slot;
            if (slot == D2_QUEUE_CAPACITY) slot = static_cast<size_t>(active_);
            failedSequence_[slot] = item->sequence; failedAt_[slot] = now;
        }
        active_ = -1;
    }
    if (active_ >= 0 || !modem_.ready() ||
        (journal_.state() != D2State::Ready && journal_.state() != D2State::SequenceExhausted)) return;
    // Premier urgent eligible, puis premiere telemetrie eligible. Pas de preemption
    // d'un CMGS en vol. Une urgence en backoff ne bloque pas les autres urgences.
    int candidate = -1;
    for (size_t i = 0; i < journal_.count(); ++i) {
        const auto* item = journal_.at(i); bool eligible = true;
        for (size_t j = 0; j < D2_QUEUE_CAPACITY; ++j) if (failedSequence_[j] == item->sequence) {
            if (now - failedAt_[j] < retryDelay(item->attempts, retryBase_, retryMax_)) eligible = false;
            else failedSequence_[j] = 0;
        }
        if (!eligible) continue;
        if (candidate < 0) candidate = static_cast<int>(i);
        if (item->urgent) { candidate = static_cast<int>(i); break; }
    }
    if (candidate < 0 || !journal_.recordAttempt(static_cast<size_t>(candidate))) return;
    if (modem_.submit(journal_.identity().receiver, journal_.at(static_cast<size_t>(candidate))->text, now))
        active_ = candidate;
}
}
