#include "d2_store.h"
#include <string.h>

namespace djua {
uint32_t d2Checksum(const void* data, size_t length) {
    // Detection de corruption locale, PAS une authentification (assuree par HMAC).
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t crc = UINT32_MAX;
    for (size_t n = 0; n < length; ++n) {
        crc ^= p[n];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320UL : 0);
    }
    return ~crc;
}
namespace {
template<class T> bool check(const T& item) {
    return item.checksum == d2Checksum(&item, offsetof(T, checksum));
}
template<class T> void seal(T& item) {
    item.checksum = d2Checksum(&item, offsetof(T, checksum));
}
bool terminated(const char* text, size_t size) { return memchr(text, 0, size) != nullptr; }
}
void D2Journal::begin() {
    next_ = end_ = nextUnreserved_ = 0;
    identity_ = D2Identity{}; queue_ = D2Queue{};
    D2Sequence sequence{};
    const ReadResult idResult = store_.read("identity", &identity_, sizeof(identity_));
    const ReadResult seqResult = store_.read("sequence", &sequence, sizeof(sequence));
    const ReadResult queueResult = store_.read("queue", &queue_, sizeof(queue_));
    if (idResult == ReadResult::Missing && seqResult == ReadResult::Missing && queueResult == ReadResult::Missing) {
        state_ = D2State::Unprovisioned; return;
    }
    if (idResult != ReadResult::Ok || identity_.version != 1 || !check(identity_) ||
        !terminated(identity_.device, sizeof(identity_.device)) || !validDeviceId(identity_.device) ||
        !terminated(identity_.receiver, sizeof(identity_.receiver)) || !validE164(identity_.receiver) ||
        !terminated(identity_.metadata, sizeof(identity_.metadata))) {
        state_ = D2State::StorageFault; return;
    }
    if (seqResult != ReadResult::Ok || !check(sequence) || sequence.next < 1 ||
        sequence.next > UINT64_C(4294967296)) {
        state_ = D2State::CounterFault; return;
    }
    if (queueResult != ReadResult::Ok || queue_.version != 1 || !check(queue_) ||
        queue_.count > D2_QUEUE_CAPACITY) { state_ = D2State::StorageFault; return; }
    for (size_t i = 0; i < queue_.count; ++i) {
        const auto& item = queue_.items[i]; D2Frame frame;
        if (!terminated(item.text, sizeof(item.text))) { state_ = D2State::StorageFault; return; }
        D2Error decoded = D2Error::InvalidFields;
        if (!strncmp(item.text, "D3T,", 4)) {
            TelemetryV3 v3;
            decoded = decodeD3(item.text, identity_.key, true, hmac_, v3);
            strcpy(frame.device, v3.device); frame.sequence = v3.sequence;
        } else decoded = decodeD2(item.text, identity_.key, true, hmac_, frame);
        if (decoded != D2Error::Ok ||
            strcmp(frame.device, identity_.device) || frame.sequence != item.sequence ||
            frame.event != item.urgent || item.sequence >= sequence.next) {
            state_ = D2State::StorageFault; return;
        }
        for (size_t j = 0; j < i; ++j) if (queue_.items[j].sequence == item.sequence) {
            state_ = D2State::StorageFault; return;
        }
    }
    nextUnreserved_ = sequence.next;
    state_ = sequence.next > UINT32_MAX ? D2State::SequenceExhausted : D2State::Ready;
}
bool D2Journal::provision(const char* device, const uint8_t key[32], const char* receiver,
                          const char* metadata, uint64_t initial) {
    if (state_ != D2State::Unprovisioned || !validDeviceId(device) || !key ||
        !validE164(receiver) || !metadata || !*metadata || strlen(metadata) > 64 ||
        initial < 1 || initial > UINT32_MAX) return false;
    // Relecture : une programmation interrompue ne peut etre reprise au compteur 1.
    D2Identity previous{}; D2Sequence seq{}; char oldQueue = 0;
    if (store_.read("identity", &previous, sizeof(previous)) != ReadResult::Missing ||
        store_.read("sequence", &seq, sizeof(seq)) != ReadResult::Missing ||
        store_.read("queue", &oldQueue, sizeof(oldQueue)) != ReadResult::Missing) {
        state_ = D2State::StorageFault; return false;
    }
    D2Identity identity{}; strcpy(identity.device, device);
    memcpy(identity.key, key, 32); strcpy(identity.receiver, receiver); strcpy(identity.metadata, metadata);
    seal(identity);
    D2Sequence sequence{}; sequence.next = initial; seal(sequence);
    D2Queue queue{}; seal(queue);
    // Identite ecrite en dernier : une interruption laisse un etat non activable.
    if (!store_.write("sequence", &sequence, sizeof(sequence)) ||
        !store_.write("queue", &queue, sizeof(queue)) ||
        !store_.write("identity", &identity, sizeof(identity))) {
        state_ = D2State::StorageFault; return false;
    }
    begin(); return state_ == D2State::Ready;
}
bool D2Journal::nextSequence(uint32_t& sequence) {
    sequence = 0;
    if (state_ != D2State::Ready) return false;
    if (next_ == end_) {
        if (nextUnreserved_ > UINT32_MAX) { state_ = D2State::SequenceExhausted; return false; }
        D2Sequence record{};
        record.next = nextUnreserved_ + 256;
        if (record.next > UINT64_C(4294967296)) record.next = UINT64_C(4294967296);
        seal(record);
        if (!store_.write("sequence", &record, sizeof(record))) {
            state_ = D2State::CounterFault; return false;
        }
        next_ = nextUnreserved_; end_ = nextUnreserved_ = record.next;
    }
    sequence = static_cast<uint32_t>(next_++); return true;
}
bool D2Journal::saveQueue(D2Queue& candidate) {
    seal(candidate);
    if (!store_.write("queue", &candidate, sizeof(candidate))) {
        state_ = D2State::StorageFault; return false;
    }
    queue_ = candidate; return true;
}
EnqueueResult D2Journal::enqueue(D2Frame frame, D2Error& codecError) {
    codecError = D2Error::Ok;
    if (state_ != D2State::Ready) return EnqueueResult::NotReady;
    if (queue_.count >= D2_QUEUE_CAPACITY) return EnqueueResult::Full;
    if (!frame.event) {
        size_t periodic = 0;
        for (size_t i = 0; i < queue_.count; ++i) if (!queue_.items[i].urgent) ++periodic;
        if (periodic >= D2_PERIODIC_CAPACITY) return EnqueueResult::Full;
    }
    // Consommer une sequence meme si la construction echoue est sans danger.
    strcpy(frame.device, identity_.device);
    if (!nextSequence(frame.sequence)) return EnqueueResult::NotReady;
    D2QueuedSms item{}; item.sequence = frame.sequence; item.urgent = frame.event;
    codecError = frame.event ? encodeD2(frame, identity_.key, true, hmac_, item.text)
                            : encodeD2T2(frame, identity_.key, true, hmac_, item.text);
    if (codecError != D2Error::Ok) return EnqueueResult::CodecError;
    D2Queue candidate = queue_; candidate.items[candidate.count++] = item;
    return saveQueue(candidate) ? EnqueueResult::Stored : EnqueueResult::StorageError;
}
const D2QueuedSms* D2Journal::at(size_t index) const { return index < queue_.count ? &queue_.items[index] : nullptr; }
EnqueueResult D2Journal::enqueueV3(TelemetryV3 frame, D2Error& codecError) {
    codecError = D2Error::Ok;
    if (state_ != D2State::Ready) return EnqueueResult::NotReady;
    size_t periodic = 0;
    for (size_t i = 0; i < queue_.count; ++i) if (!queue_.items[i].urgent) ++periodic;
    if (queue_.count + 2 > D2_QUEUE_CAPACITY || periodic + 2 > D2_PERIODIC_CAPACITY) return EnqueueResult::Full;
    strcpy(frame.device, identity_.device);
    if (!nextSequence(frame.snapshot) || frame.snapshot == UINT32_MAX) return EnqueueResult::NotReady;
    D2Queue candidate = queue_;
    for (uint8_t part = 0; part < 2; ++part) {
        frame.part = part;
        if (part == 0) frame.sequence = frame.snapshot;
        else if (!nextSequence(frame.sequence)) return EnqueueResult::NotReady;
        D2QueuedSms item{}; item.sequence = frame.sequence;
        codecError = encodeD3(frame, identity_.key, true, hmac_, item.text);
        if (codecError != D2Error::Ok) return EnqueueResult::CodecError;
        candidate.items[candidate.count++] = item;
    }
    // One atomic NVS blob commit: never persist just the first part.
    return saveQueue(candidate) ? EnqueueResult::Stored : EnqueueResult::StorageError;
}
bool D2Journal::recordAttempt(size_t index) {
    if (index >= queue_.count || (state_ != D2State::Ready && state_ != D2State::SequenceExhausted)) return false;
    D2Queue candidate = queue_;
    if (candidate.items[index].attempts < UINT32_MAX) ++candidate.items[index].attempts;
    return saveQueue(candidate);
}
bool D2Journal::acceptedByModem(size_t index) {
    if (index >= queue_.count || (state_ != D2State::Ready && state_ != D2State::SequenceExhausted)) return false;
    D2Queue candidate = queue_;
    for (size_t i = index; i + 1 < candidate.count; ++i) candidate.items[i] = candidate.items[i + 1];
    candidate.items[--candidate.count] = D2QueuedSms{};
    return saveQueue(candidate);
}
const char* d2StateName(D2State state) {
    switch (state) {
    case D2State::Unprovisioned: return "D2_UNPROVISIONED";
    case D2State::Ready: return "D2_READY";
    case D2State::CounterFault: return "D2_COUNTER_FAULT";
    case D2State::StorageFault: return "D2_STORAGE_FAULT";
    case D2State::SequenceExhausted: return "D2_SEQUENCE_EXHAUSTED";
    }
    return "D2_UNKNOWN";
}
}
