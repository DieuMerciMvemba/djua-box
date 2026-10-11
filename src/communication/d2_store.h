#pragma once
#include "d2_codec.h"
#include "d3_codec.h"

namespace djua {
enum class ReadResult { Missing, Ok, Error };
class D2Store {
public:
    virtual ~D2Store() = default;
    virtual ReadResult read(const char* name, void* data, size_t size) = 0;
    virtual bool write(const char* name, const void* data, size_t size) = 0;
};
enum class D2State { Unprovisioned, Ready, CounterFault, StorageFault, SequenceExhausted };
enum class EnqueueResult { Stored, NotReady, Full, CodecError, StorageError };
struct D2Identity {
    uint32_t version = 1;
    char device[33] = {};
    uint8_t key[32] = {};
    char receiver[17] = {};
    char metadata[65] = {};
    uint32_t checksum = 0;
};
struct D2Sequence { uint64_t next = 1; uint32_t checksum = 0; };
struct D2QueuedSms {
    char text[D2_SMS_CAPACITY] = {};
    uint32_t sequence = 0, attempts = 0;
    bool urgent = false;
};
constexpr size_t D2_QUEUE_CAPACITY = 16;
constexpr size_t D2_PERIODIC_CAPACITY = 4; // douze places garanties aux urgences
struct D2Queue {
    uint32_t version = 1, count = 0;
    D2QueuedSms items[D2_QUEUE_CAPACITY];
    uint32_t checksum = 0;
};

class D2Journal {
public:
    explicit D2Journal(D2Store& store, D2Hmac hmac) : store_(store), hmac_(hmac) {}
    void begin();
    // Seulement sur NVS vierge. Aucun reset/reprovisionnement implicite.
    bool provision(const char* device, const uint8_t key[32], const char* receiver,
                   const char* metadata, uint64_t initial = 1);
    D2State state() const { return state_; }
    const D2Identity& identity() const { return identity_; }
    EnqueueResult enqueue(D2Frame frame, D2Error& codecError);
    EnqueueResult enqueueV3(TelemetryV3 frame, D2Error& codecError);
    size_t count() const { return queue_.count; }
    const D2QueuedSms* at(size_t index) const;
    bool recordAttempt(size_t index);
    bool acceptedByModem(size_t index);
    bool nextSequence(uint32_t& sequence);
private:
    bool saveQueue(D2Queue& candidate);
    D2Store& store_;
    D2Hmac hmac_;
    D2State state_ = D2State::Unprovisioned;
    D2Identity identity_{};
    D2Queue queue_{};
    uint64_t next_ = 0, end_ = 0, nextUnreserved_ = 0;
};
const char* d2StateName(D2State state);
uint32_t d2Checksum(const void* data, size_t length);
}
