#include "sms.h"
#include "gsm.h"
#include "sms_scheduler.h"
#include "gsm_startup.h"
#include "../config.h"
#include "../pins.h"
#if DJUA_COMM_PROFILE == DJUA_PROFILE_SMS_PRODUCTION
#include <Arduino.h>
#include <Preferences.h>
#include <mbedtls/md.h>
#include <string.h>
#include "../status/device_status.h"

namespace {
bool espHmac(const uint8_t key[32], const char* data, size_t length, uint8_t digest[32]) {
    const auto* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return info && mbedtls_md_hmac(info, key, 32,
        reinterpret_cast<const unsigned char*>(data), length, digest) == 0;
}
class NvsStore : public djua::D2Store {
public:
    bool begin() { open_ = preferences_.begin("djua_d2", false); return open_; }
    djua::ReadResult read(const char* name, void* data, size_t size) override {
        if (!open_) return djua::ReadResult::Error;
        if (!preferences_.isKey(name)) return djua::ReadResult::Missing;
        if (preferences_.getBytesLength(name) != size || preferences_.getBytes(name, data, size) != size)
            return djua::ReadResult::Error;
        return djua::ReadResult::Ok;
    }
    bool write(const char* name, const void* data, size_t size) override {
        // Preferences::putBytes commit la NVS. Une taille partielle est un echec.
        return open_ && preferences_.putBytes(name, data, size) == size;
    }
private:
    Preferences preferences_;
    bool open_ = false;
};
class UartIo : public djua::GsmIo {
public:
    HardwareSerial uart{1};
    bool open = false;
    int available() override { return open ? uart.available() : 0; }
    int read() override { return open ? uart.read() : -1; }
    bool write(const char* data, size_t size) override {
        return open && uart.availableForWrite() >= static_cast<int>(size) &&
            uart.write(reinterpret_cast<const uint8_t*>(data), size) == size;
    }
};
NvsStore store;
UartIo io;
djua::D2Journal journal(store, espHmac);
djua::GsmEngine modem(io, GSM_COMMAND_TIMEOUT_MS, GSM_SUBMIT_TIMEOUT_MS, GSM_RETRY_INTERVAL_MS);
djua::SmsScheduler scheduler(journal, modem, GSM_RETRY_INTERVAL_MS, SMS_RETRY_MAX_MS);
bool healthy = false, queueFault = false;
djua::GsmStartup startup(GSM_POWER_STABILIZE_MS, GSM_PWRKEY_PULSE_MS, GSM_BOOT_WAIT_MS);
bool modemStarted = false;
uint32_t lastSubmit = 0;
djua::D2State lastState = djua::D2State::Unprovisioned;
djua::GsmFault lastModemFault = djua::GsmFault::None;

#if GSM_HARDWARE_CONFIGURED
constexpr bool occupiedPin(int pin) {
    return pin == GPS_RX_PIN || pin == GPS_TX_PIN || pin == I2C_SDA_PIN || pin == I2C_SCL_PIN ||
        pin == DS1302_DATA_PIN || pin == DS1302_CLOCK_PIN || pin == DS1302_CE_PIN ||
        (pin == PZEM_RX_PIN || pin == PZEM_TX_PIN) || pin == LED_SYSTEM_PIN ||
        pin == LED_COMM_PIN || pin == LED_ALERT_PIN || pin == BUZZER_PIN;
}
constexpr bool uartPin(int pin, bool tx) {
    return pin >= 0 && pin <= (tx ? 33 : 39) && pin != 0 && pin != 1 && pin != 2 &&
        pin != 3 && pin != 5 && !(pin >= 6 && pin <= 12) && pin != 15 &&
        pin != 20 && pin != 24 && !(pin >= 28 && pin <= 31) && pin != 37 && pin != 38;
}
static_assert(uartPin(GSM_RX_PIN, false) && uartPin(GSM_TX_PIN, true) &&
              GSM_RX_PIN != GSM_TX_PIN && !occupiedPin(GSM_RX_PIN) && !occupiedPin(GSM_TX_PIN),
              "Broches GSM invalides, strapping ou deja utilisees");
static_assert(GSM_PWRKEY_PIN == -1 || (uartPin(GSM_PWRKEY_PIN, true) &&
    !occupiedPin(GSM_PWRKEY_PIN) && GSM_PWRKEY_PIN != GSM_RX_PIN && GSM_PWRKEY_PIN != GSM_TX_PIN),
    "PWRKEY invalide ou deja utilise");
static_assert(GSM_PWRKEY_ACTIVE_LEVEL == 0 || GSM_PWRKEY_ACTIVE_LEVEL == 1,
              "Niveau actif PWRKEY invalide");
#endif

#if D2_PROVISIONING_ENABLED
char command[256] = {}; size_t commandUsed = 0; bool commandOverflow = false;
void provisionCommand() {
    char* save = nullptr;
    char* action = strtok_r(command, " ", &save);
    if (!action || strcmp(action, "D2")) return;
    action = strtok_r(nullptr, " ", &save);
    if (!action) return;
    if (!strcmp(action, "STATUS")) {
        Serial.printf("[D2] %s | file=%u | modem=%s\n", djua::d2StateName(journal.state()),
            static_cast<unsigned>(journal.count()), GSM_MODULE_LABEL);
        return;
    }
    if (strcmp(action, "PROVISION")) return;
    char* device = strtok_r(nullptr, " ", &save);
    char* hex = strtok_r(nullptr, " ", &save);
    char* receiver = strtok_r(nullptr, " ", &save);
    char* metadata = strtok_r(nullptr, " ", &save);
    char* counter = strtok_r(nullptr, " ", &save);
    uint8_t key[32] = {}; bool valid = device && hex && receiver && metadata && counter &&
        !strtok_r(nullptr, " ", &save) && strlen(hex) == 64;
    if (valid) for (size_t i = 0; i < 64; ++i) {
        const char c = hex[i];
        const int digit = c >= '0' && c <= '9' ? c - '0' :
            c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (digit < 0) { valid = false; break; }
        key[i / 2] = static_cast<uint8_t>((key[i / 2] << 4) | digit);
    }
    uint64_t initial = 0;
    if (valid) for (const char* p = counter; *p; ++p) {
        if (*p < '0' || *p > '9' || initial > UINT32_MAX / 10ULL) { valid = false; break; }
        initial = initial * 10 + static_cast<unsigned>(*p - '0');
    }
    // La cle de conformite publique ne doit pas servir de secret terrain.
    bool publicTestKey = true;
    for (size_t i = 0; i < 32; ++i) publicTestKey = publicTestKey && key[i] == i;
    const bool result = valid && !publicTestKey && initial <= UINT32_MAX &&
        journal.provision(device, key, receiver, metadata, initial);
    memset(key, 0, sizeof(key));
    Serial.println(result ? "[D2] Provisionnement durable OK" : "[D2] Provisionnement REFUSE (parametres ou NVS non vierge)");
}
void updateProvisioning() {
    for (size_t i = 0; i < 64 && Serial.available(); ++i) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\n') {
            if (!commandOverflow) { command[commandUsed] = 0; provisionCommand(); }
            else Serial.println("[D2] Commande trop longue : REFUSEE");
            memset(command, 0, sizeof(command)); commandUsed = 0; commandOverflow = false;
        } else if (c != '\r') {
            if (commandUsed + 1 < sizeof(command)) command[commandUsed++] = c;
            else commandOverflow = true;
        }
    }
}
#endif
void autoProvisionIfAbsent() {
    if (journal.state() != djua::D2State::Unprovisioned) return;
    const char* hex = D2_DEFAULT_HMAC_KEY_HEX;
    if (!hex || strlen(hex) != 64) return;
    uint8_t key[32] = {};
    for (size_t i = 0; i < 64; ++i) {
        const char c = hex[i];
        const int digit = c >= '0' && c <= '9' ? c - '0' :
            c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (digit < 0) { memset(key, 0, sizeof(key)); return; }
        key[i / 2] = static_cast<uint8_t>((key[i / 2] << 4) | digit);
    }
    const bool ok = journal.provision(DEVICE_ID, key, D2_DEFAULT_RECEIVER, "firmware-default", 1);
    memset(key, 0, sizeof(key));
    Serial.println(ok ? "[D2] Provisionnement auto NVS OK" : "[D2] Provisionnement auto NVS REFUSE");
}
}
void initSMS() {
    store.begin(); journal.begin();
    autoProvisionIfAbsent();
    Serial.printf("[D2] reference=%s | etat=%s\n", D2_REFERENCE_SHA, djua::d2StateName(journal.state()));
#if GSM_HARDWARE_CONFIGURED
    io.uart.setRxBufferSize(512);
    io.uart.setTxBufferSize(256);
    io.uart.begin(GSM_BAUD_RATE, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);
    io.open = true;
    if (GSM_PWRKEY_PIN >= 0) {
        digitalWrite(GSM_PWRKEY_PIN, !GSM_PWRKEY_ACTIVE_LEVEL);
        pinMode(GSM_PWRKEY_PIN, OUTPUT);
    }
    Serial.printf("[GSM] UART1 configure | module=%s | RX=%d | TX=%d | baud=%lu\n",
                  GSM_MODULE_LABEL, GSM_RX_PIN, GSM_TX_PIN, GSM_BAUD_RATE);
#else
    Serial.println("[GSM] NON CONFIGURE : aucun UART/PWRKEY active, aucun SMS envoye");
#endif
    modem.begin(false, millis());
    startup.begin(GSM_PWRKEY_PIN >= 0, millis()); modemStarted = false;
    lastState = journal.state();
#if D2_PROVISIONING_ENABLED
    Serial.println("[D2] Console USB de provisionnement active ; ne jamais publier les cles");
#endif
}
bool enqueueSMS(djua::D2Frame frame) {
    djua::D2Error error; const auto result = journal.enqueue(frame, error);
    if (result != djua::EnqueueResult::Stored) {
        queueFault = true;
        Serial.printf("[D2] MESSAGE NON CREE | resultat=%u | %s | %s | file=%u\n",
            static_cast<unsigned>(result), djua::d2StateName(journal.state()), djua::d2ErrorName(error),
            static_cast<unsigned>(journal.count()));
        recordSMSTelemetryStatus(false); return false;
    }
    queueFault = false;
    Serial.printf("[D2] %s durablement enregistre | file=%u\n", frame.event ? "URGENCE" : "PERIODIQUE",
        static_cast<unsigned>(journal.count()));
    return true;
}
void updateSMS() {
#if D2_PROVISIONING_ENABLED
    updateProvisioning();
#endif
    const uint32_t now = millis();
    if (io.open && !modemStarted) {
        startup.update(now);
#if GSM_HARDWARE_CONFIGURED
        if (GSM_PWRKEY_PIN >= 0)
            digitalWrite(GSM_PWRKEY_PIN, startup.asserted() ? GSM_PWRKEY_ACTIVE_LEVEL : !GSM_PWRKEY_ACTIVE_LEVEL);
#endif
        if (startup.ready()) { modem.begin(true, now); modemStarted = true; }
    }
    scheduler.update(now);
    if (lastModemFault != modem.fault()) {
        lastModemFault = modem.fault();
        const char* reason = lastModemFault == djua::GsmFault::SimNotReady ? "SIM ABSENTE/VERROUILLEE (PIN/PUK)" :
            lastModemFault == djua::GsmFault::SmsUnsupported ? "SERVICE SMS MO NON DISPONIBLE" :
            lastModemFault == djua::GsmFault::ServiceCentreMissing ? "CENTRE SMS NON CONFIGURE" :
            lastModemFault == djua::GsmFault::NotRegistered ? "RESEAU NON ENREGISTRE" :
            lastModemFault == djua::GsmFault::SubmitTimeout ? "CMGS INCERTAIN/TIMEOUT" :
            lastModemFault == djua::GsmFault::PromptTimeout ? "COMMANDE/PROMPT TIMEOUT" :
            lastModemFault == djua::GsmFault::None ? "MODEM PRET" : "ERREUR AT/UART";
        Serial.printf("[GSM] %s | code_erreur=%d\n", reason, modem.errorCode());
    }
    const auto result = scheduler.takeResult();
    if (result != djua::GsmResult::None) {
        healthy = result == djua::GsmResult::AcceptedByModem;
        if (healthy) lastSubmit = millis();
        recordSMSTelemetryStatus(healthy);
        Serial.println(healthy ? "[SMS] Accepte par modem/operateur ; reception gateway/backend NON CONFIRMEE"
                               : "[SMS] Echec ou resultat incertain ; meme SMS conserve pour retry");
    }
    if (lastState != journal.state()) {
        lastState = journal.state(); Serial.printf("[D2] %s\n", djua::d2StateName(lastState));
    }
}
bool enqueueSMSV3(djua::TelemetryV3 frame) {
    if (!D3_RECEIVER_ENABLED) return false;
    djua::D2Error error; const auto result = journal.enqueueV3(frame, error);
    queueFault = result != djua::EnqueueResult::Stored;
    if (queueFault) recordSMSTelemetryStatus(false);
    Serial.printf("[D3] paire=%s | erreur=%s | file=%u\n", queueFault ? "NON CREEE" : "ENREGISTREE EN NVS",
        djua::d2ErrorName(error), static_cast<unsigned>(journal.count()));
    return !queueFault;
}
bool isSMSNetworkRegistered() { return modem.registered(); }
bool isSMSConnecting() { return io.open && (!modemStarted || !modem.ready()); }
bool isSMSHardwareConfigured() { return io.open; }
bool isSMSHealthy() { return healthy && millis() - lastSubmit < 3UL * D2_PERIOD_SECONDS * 1000UL; }
bool isSMSFaulted() { return queueFault || (journal.state() != djua::D2State::Ready); }
bool hasPendingSMS() { return journal.count() > 0; }
djua::D2State smsProvisioningState() { return journal.state(); }
#else
void initSMS() {}
void updateSMS() {}
bool enqueueSMS(djua::D2Frame) { return false; }
bool enqueueSMSV3(djua::TelemetryV3) { return false; }
bool isSMSNetworkRegistered() { return false; }
bool isSMSConnecting() { return false; }
bool isSMSHardwareConfigured() { return false; }
bool isSMSHealthy() { return false; }
bool isSMSFaulted() { return false; }
bool hasPendingSMS() { return false; }
djua::D2State smsProvisioningState() { return djua::D2State::Unprovisioned; }
#endif
