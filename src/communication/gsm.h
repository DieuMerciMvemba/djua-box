#pragma once
#include "d2_codec.h"

namespace djua {
class GsmIo {
public:
    virtual ~GsmIo() = default;
    virtual int available() = 0;
    virtual int read() = 0;
    virtual bool write(const char* data, size_t length) = 0;
};
enum class GsmState { Disabled, Backoff, Initializing, Ready, Prompt, Submitting };
enum class GsmResult { None, AcceptedByModem, Failed };
enum class GsmModel { Sim800, A7670 };
enum class GsmFault { None, SimNotReady, NotRegistered, CommandError, WriteError,
                      PromptTimeout, SubmitTimeout, LineTooLong, UnexpectedOk,
                      SmsUnsupported, ServiceCentreMissing };
class GsmEngine {
public:
    explicit GsmEngine(GsmIo& io, uint32_t commandTimeout = 5000,
                       uint32_t submitTimeout = 60000, uint32_t retryInterval = 30000,
                       GsmModel model = GsmModel::Sim800)
        : io_(io), commandTimeout_(commandTimeout), submitTimeout_(submitTimeout), retryInterval_(retryInterval), model_(model) {}
    void begin(bool configured, uint32_t now);
    void update(uint32_t now);
    bool submit(const char* receiver, const char* sms, uint32_t now);
    bool ready() const { return state_ == GsmState::Ready; }
    bool registered() const { return registered_; }
    bool lteRegistered() const { return lteRegistered_; }
    bool smsConfigured() const { return smsSupported_ && serviceCentre_; }
    int errorCode() const { return errorCode_; }
    bool configured() const { return state_ != GsmState::Disabled; }
    bool busy() const { return state_ == GsmState::Prompt || state_ == GsmState::Submitting; }
    GsmResult takeResult();
    GsmState state() const { return state_; }
    GsmFault fault() const { return fault_; }
private:
    void startCommand(uint32_t now);
    void line(uint32_t now);
    void fail(uint32_t now, GsmFault fault);
    GsmIo& io_;
    GsmState state_ = GsmState::Disabled;
    GsmResult result_ = GsmResult::None;
    uint32_t since_ = 0, lastCheck_ = 0;
    uint32_t commandTimeout_, submitTimeout_, retryInterval_;
    GsmFault fault_ = GsmFault::None;
    GsmModel model_;
    bool lteRegistered_ = false, csRegistered_ = false;
    bool smsSupported_ = false, serviceCentre_ = false;
    int errorCode_ = -1;
    uint8_t step_ = 0;
    bool simReady_ = false, registered_ = false, sawCmgs_ = false, polling_ = false;
    char line_[192] = {}, payload_[D2_SMS_CAPACITY] = {};
    size_t used_ = 0;
};
}
