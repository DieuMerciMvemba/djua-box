#pragma once
#include <stddef.h>
#include <stdint.h>

namespace djua {
constexpr size_t D2_SMS_CAPACITY = 161;
enum class D2Error {
    Ok, NonGsm7, SmsTooLong, InvalidFields, InvalidDevice, NonCanonicalBase36,
    OutOfRange, FlagValueMismatch, AuthRequired, AuthInvalid, CryptoFailure,
    InvalidFlags, NonCanonicalAbsent, FlagDependency, PayloadShape, ReservedBits
};
enum D2Field { Lat, Lon, BatteryV, BatteryI, BatteryP, SolarV, SolarI,
               SolarP, SolarE, AcV, AcI, AcP, AcS, AcE, Distance,
               DcV, DcI, DcP, DcE, FieldCount };
constexpr size_t D2_LEGACY_FIELD_COUNT = Distance + 1;
struct D2Value { double value = 0; bool present = false; };
struct D2Frame {
    bool event = false;
    bool exit = true;
    uint8_t dcMeasuredCount = 0;
    bool dcMeasuredCountPresent = true;
    char device[33] = {};
    uint32_t sequence = 0, rtc = 0, uptime = 0, interval = 0;
    uint16_t flags = 0;
    D2Value values[FieldCount];
};
// ESP32 : mbedTLS ; tests PC : fournisseur cryptographique du systeme.
using D2Hmac = bool (*)(const uint8_t key[32], const char* data, size_t length,
                       uint8_t digest[32]);
bool validDeviceId(const char* id);
bool validE164(const char* number);
D2Error countD2Septets(const char* sms, size_t& count);
D2Error encodeD2(const D2Frame& frame, const uint8_t* key, bool requireAuth,
                 D2Hmac hmac, char output[D2_SMS_CAPACITY]);
D2Error decodeD2(const char* sms, const uint8_t* key, bool requireAuth,
                 D2Hmac hmac, D2Frame& frame);
// D2T is retained for immutable legacy NVS entries; periodic production uses D2T2.
// D3 decoding remains for already-persisted entries; geofence events keep D2E.
constexpr size_t D2T2_PAYLOAD_BYTES = 60;
D2Error packD2T2(const D2Frame& frame, uint8_t output[D2T2_PAYLOAD_BYTES]);
D2Error encodeD2T2(const D2Frame& frame, const uint8_t* key, bool requireAuth,
                   D2Hmac hmac, char output[D2_SMS_CAPACITY]);
D2Error decodeD2T2(const char* sms, const uint8_t* key, bool requireAuth,
                   D2Hmac hmac, D2Frame& frame);
bool d2MakeTag(const uint8_t* key, D2Hmac hmac, const char* data, size_t length, char tag[12]);
const char* d2ErrorName(D2Error error);
// Conversion pure de la date locale DS1302 en epoch UTC, sans heure inventee.
bool d2UtcEpoch(uint16_t year, uint8_t month, uint8_t day, uint8_t hour,
                uint8_t minute, uint8_t second, int offsetMinutes, uint32_t& epoch);
}
