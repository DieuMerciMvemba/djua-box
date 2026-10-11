#include <Arduino.h>
#include <Preferences.h>
#include <mbedtls/md.h>
#include <string.h>
#include "../../../src/communication/d2_store.h"
#include "../../../src/communication/gsm.h"

// Isolated GSM bench on final RX32/TX13; production GPS keeps GPIO 16/17.
// No GPS, sensors, Wi-Fi, MQTT, HTTP or automatic SMS in this sketch.
namespace {
constexpr char DEVICE[] = "DJUA-TEST-20261006-A";
constexpr char RECEIVER[] = "+243852869410";
constexpr char NVS_NAMESPACE[] = "djua_d2_bench";
constexpr uint32_t FIRST_SEQUENCE = 5; // 1..4 already used in the manual tests.
constexpr int MODEM_RX = 32, MODEM_TX = 13;
constexpr uint32_t MODEM_BAUD = 115200;

void wipe(void* data, size_t length) {
    volatile uint8_t* p = static_cast<volatile uint8_t*>(data);
    while (length--) *p++ = 0;
}

bool espHmac(const uint8_t key[32], const char* data, size_t length, uint8_t digest[32]) {
    const auto* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return info && mbedtls_md_hmac(info, key, 32,
        reinterpret_cast<const unsigned char*>(data), length, digest) == 0;
}

class BenchStore : public djua::D2Store {
public:
    bool begin() { open_ = prefs_.begin(NVS_NAMESPACE, false); return open_; }
    djua::ReadResult read(const char* name, void* out, size_t length) override {
        if (!open_) return djua::ReadResult::Error;
        if (!prefs_.isKey(name)) return djua::ReadResult::Missing;
        return prefs_.getBytesLength(name) == length && prefs_.getBytes(name, out, length) == length
            ? djua::ReadResult::Ok : djua::ReadResult::Error;
    }
    bool write(const char* name, const void* data, size_t length) override {
        return open_ && prefs_.putBytes(name, data, length) == length;
    }
private:
    Preferences prefs_;
    bool open_ = false;
};

class BenchUart : public djua::GsmIo {
public:
    HardwareSerial uart{1};
    int available() override { return uart.available(); }
    int read() override {
        const int value = uart.read();
        if (value >= 0) Serial.write(static_cast<uint8_t>(value));
        return value;
    }
    bool write(const char* data, size_t length) override {
        // Never log USB provisioning commands; this transport only carries AT/SMS.
        if (uart.availableForWrite() < static_cast<int>(length)) return false;
        return uart.write(reinterpret_cast<const uint8_t*>(data), length) == length;
    }
};

BenchStore store;
BenchUart io;
djua::D2Journal journal(store, espHmac);
djua::GsmEngine modem(io);
bool cryptoOk = false, modemStarted = false, wasReady = false;
bool active = false, createdThisBoot = false;
uint32_t bootSince = 0, activeSequence = 0;
char input[128] = {};
size_t inputUsed = 0;
bool overflow = false;

djua::D2Frame referenceFrame() {
    djua::D2Frame frame;
    strcpy(frame.device, "DJUA-KIN-000001");
    frame.sequence = 12345; frame.rtc = 1790523000; frame.uptime = 123456789;
    frame.interval = 1800; frame.flags = 511;
    const double values[] = {-4.3251,15.3222,12.4,0.5,6.2,18.2,1.23,22.4,11.2,
                            230.1,1.25,244.5,287.6,122.25,0,12.35,3.2,39.5,19.76};
    for (size_t i = 0; i < djua::FieldCount; ++i)
        if (i != djua::Distance) frame.values[i] = {values[i], true};
    return frame;
}
bool selfTest() {
    // Public official conformance key: RAM self-test only, never provisioned/sent.
    uint8_t key[32];
    for (size_t i = 0; i < sizeof(key); ++i) key[i] = static_cast<uint8_t>(i);
    djua::D2Frame frame = referenceFrame();
    char sms[djua::D2_SMS_CAPACITY] = {};
    const char* expected = "D2T2,DJUA-KIN-000001,AAAwOWq5NngHW80VA4R-WaCBdhPGDgAH0AB8RxgBM4AOAAARgj9B9AmNBZ4ABfgjA-AyAAGLAAHuAH_A,rHcRllcP4gs";
    bool ok = djua::encodeD2T2(frame, key, true, espHmac, sms) == djua::D2Error::Ok &&
        strcmp(sms, expected) == 0;
    djua::D2Frame decoded;
    ok = ok && djua::decodeD2(sms, key, true, espHmac, decoded) == djua::D2Error::Ok;
    if (ok) {
        sms[strlen(sms) - 1] = 'A'; // Alter AUTH: must be refused.
        ok = djua::decodeD2(sms, key, true, espHmac, decoded) == djua::D2Error::AuthInvalid;
    }
    wipe(key, sizeof(key));
    return ok;
}

bool benchIdentity() {
    return journal.state() == djua::D2State::Ready &&
        strcmp(journal.identity().device, DEVICE) == 0 &&
        strcmp(journal.identity().receiver, RECEIVER) == 0;
}

void status() {
    Serial.printf("\n[BENCH] FW=D2T2_ESP32_HMAC_V2 STATE=%s ID=%s PENDING=%u CRYPTO=%s MODEM=%s\n",
        djua::d2StateName(journal.state()),
        journal.state() == djua::D2State::Ready ? journal.identity().device : DEVICE,
        static_cast<unsigned>(journal.count()),
        cryptoOk ? "OK" : "FAIL", modem.ready() ? "READY" : "WAIT");
    if (journal.state() == djua::D2State::Ready && !benchIdentity())
        Serial.println("[BENCH] IDENTITY_MISMATCH: no reset or overwrite permitted.");
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void provision(const char* hex) {
    uint8_t key[32] = {};
    bool valid = cryptoOk && strlen(hex) == 64;
    if (valid) for (size_t i = 0; i < 64; ++i) {
        const int digit = hexDigit(hex[i]);
        if (digit < 0) { valid = false; break; }
        key[i / 2] = static_cast<uint8_t>((key[i / 2] << 4) | digit);
    }
    bool publicKey = true;
    for (size_t i = 0; i < 32; ++i) publicKey = publicKey && key[i] == i;
    valid = valid && !publicKey;
    bool ok = false;
    if (valid && benchIdentity()) {
        uint8_t mismatch = 0;
        for (size_t i = 0; i < 32; ++i) mismatch |= key[i] ^ journal.identity().key[i];
        ok = mismatch == 0; // Confirm an existing key; never replace it/reset sequence.
    } else if (valid && journal.state() == djua::D2State::Unprovisioned) {
        ok = journal.provision(DEVICE, key, RECEIVER, "a7670-hmac-bench", FIRST_SEQUENCE);
    }
    wipe(key, sizeof(key));
    Serial.println(ok ? "[BENCH] KEY_OK" : "[BENCH] KEY_REFUSED");
}

void submitPending() {
    if (!cryptoOk || !benchIdentity() || !modem.ready() || !modem.registered() || active || journal.count() != 1) {
        Serial.println("[BENCH] NOT_READY: use BENCH STATUS; no SMS submitted.");
        return;
    }
    const auto* item = journal.at(0);
    if (!journal.recordAttempt(0)) {
        Serial.println("[BENCH] STORAGE_ERROR: no SMS submitted.");
        return;
    }
    // recordAttempt changes queue RAM; reacquire the item before using it.
    item = journal.at(0);
    activeSequence = item->sequence;
    if (!modem.submit(RECEIVER, item->text, millis())) {
        Serial.println("[BENCH] SUBMIT_REFUSED: pending SMS preserved; use RETRY when ready.");
        return;
    }
    active = true;
    Serial.printf("\n[BENCH] SUBMIT sequence=%lu\n", static_cast<unsigned long>(activeSequence));
    Serial.println(item->text); // Signed test SMS, never the key.
}

void send(bool reference = false) {
    if (!cryptoOk || !benchIdentity() || !modem.ready() || !modem.registered() || active) {
        Serial.println("[BENCH] NOT_READY: no new message created."); return;
    }
    if (journal.count()) {
        Serial.println("[BENCH] PENDING_EXISTS: RETRY resends identical bytes; no new sequence."); return;
    }
    if (createdThisBoot) {
        Serial.println("[BENCH] ONE_MESSAGE_PER_BOOT: no new SMS created."); return;
    }
    djua::D2Frame frame = reference ? referenceFrame() : djua::D2Frame{};
    frame.uptime = millis(); frame.interval = 1800;
    // SEND: all measurements absent. SEND VECTOR: explicitly synthetic fixture,
    // signed with the private bench key and a newly reserved bench sequence.
    if (reference) Serial.println("[BENCH] SYNTHETIC_VECTOR: these are NOT physical measurements.");
    djua::D2Error error;
    const auto result = journal.enqueue(frame, error);
    if (result != djua::EnqueueResult::Stored) {
        Serial.printf("[BENCH] ENQUEUE_REFUSED result=%u codec=%s\n",
            static_cast<unsigned>(result), djua::d2ErrorName(error)); return;
    }
    createdThisBoot = true;
    submitPending();
}

void command() {
    // NEVER echo input: it may contain the private provisioning key.
    if (!strcmp(input, "BENCH STATUS")) status();
    else if (!strncmp(input, "BENCH KEY ", 10)) provision(input + 10);
    else if (!strcmp(input, "SEND")) send();
    else if (!strcmp(input, "SEND VECTOR")) send(true);
    else if (!strcmp(input, "RETRY")) submitPending();
    else if (!strcmp(input, "CANCEL")) {
        if (modem.busy()) {
            Serial.println("[BENCH] BUSY: do not interrupt CMGS; submission may already be committed.");
        } else {
            const char escape = 27; io.write(&escape, 1);
            Serial.println("[BENCH] ESC sent. Pending NVS SMS, if any, is NOT deleted.");
        }
    } else Serial.println("[BENCH] Commands: BENCH STATUS | SEND | SEND VECTOR | RETRY | CANCEL. Key import uses local Python tool.");
}

void console() {
    for (size_t n = 0; n < 64 && Serial.available(); ++n) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\n') {
            if (overflow) Serial.println("[BENCH] COMMAND_TOO_LONG: refused.");
            else { input[inputUsed] = 0; command(); }
            wipe(input, sizeof(input)); inputUsed = 0; overflow = false;
        } else if (c != '\r') {
            if (inputUsed + 1 < sizeof(input)) input[inputUsed++] = c;
            else overflow = true;
        }
    }
}
}

void setup() {
    Serial.begin(115200);
    cryptoOk = selfTest();
    store.begin(); journal.begin();
    io.uart.setRxBufferSize(512);
    io.uart.setTxBufferSize(256);
    io.uart.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX, MODEM_TX);
    bootSince = millis();
    Serial.printf("\n[BENCH] A7670 RX=%d TX=%d baud=%lu; GSM BENCH ONLY.\n",
                  MODEM_RX, MODEM_TX, static_cast<unsigned long>(MODEM_BAUD));
    Serial.println(cryptoOk ? "[BENCH] SELFTEST_OK: official HMAC and tamper rejection." : "[BENCH] SELFTEST_FAIL: sending disabled.");
    Serial.println("[BENCH] No automatic SMS. NVS test namespace only; no erase command.");
    status();
}

void loop() {
    console();
    const uint32_t now = millis();
    if (!modemStarted && now - bootSince >= 5000) {
        const char escape = 27; io.write(&escape, 1);
        modem.begin(true, now); modemStarted = true;
    }
    if (modemStarted) modem.update(now);
    if (modem.ready() && !wasReady) Serial.println("\n[BENCH] MODEM_READY. Use SEND only after key provisioning.");
    wasReady = modem.ready();
    const auto result = modem.takeResult();
    if (result != djua::GsmResult::None) {
        active = false;
        if (result == djua::GsmResult::AcceptedByModem) {
            const bool saved = journal.acceptedByModem(0);
            Serial.printf("\n[BENCH] MODEM_ACCEPTED sequence=%lu queue_commit=%s; gateway/backend NOT confirmed.\n",
                static_cast<unsigned long>(activeSequence), saved ? "OK" : "FAILED");
        } else {
            Serial.println("\n[BENCH] SUBMIT_FAILED_OR_UNCERTAIN: SMS kept in NVS. No automatic retry; use RETRY when ready.");
        }
    }
    delay(2);
}
