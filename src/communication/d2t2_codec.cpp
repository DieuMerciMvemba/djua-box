#include "d2_codec.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace djua {
namespace {
struct PackedSpec {
    D2Field field;
    uint8_t width, flag;
    bool signedValue;
    double scale;
    int32_t minimum, maximum;
};
// Ordered exactly as DJUA_SMS/docs/sms_protocol_d2t2.md (bits 113..457).
constexpr PackedSpec specs[] = {
    {Lat,25,1,true,100000,-9000000,9000000},
    {Lon,26,1,true,100000,-18000000,18000000},
    {BatteryV,15,2,false,1000,0,32000},
    {BatteryI,19,2,true,1000,-200000,200000},
    {BatteryP,17,2,true,10,-64000,64000},
    {SolarV,17,3,false,1000,0,100000},
    {SolarI,18,3,true,1000,-100000,100000},
    {SolarP,18,3,true,10,-100000,100000},
    {SolarE,26,5,true,100,-24000000,24000000},
    {AcV,12,4,false,10,0,4000},
    {AcI,12,4,false,100,0,3000},
    {AcP,18,4,true,10,-120000,120000},
    {AcS,17,4,false,10,0,120000},
    {AcE,26,6,true,100,-28800000,28800000},
    {DcV,17,7,false,1000,0,100000},
    {DcI,18,7,true,1000,-100000,100000},
    {DcP,18,7,true,10,-100000,100000},
    {DcE,26,8,true,100,-24000000,24000000}
};
constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
void writeBits(uint8_t* out, size_t& bit, uint32_t value, unsigned width) {
    for (unsigned n = width; n > 0; --n, ++bit)
        if ((value >> (n - 1)) & 1U) out[bit / 8] |= static_cast<uint8_t>(1U << (7 - bit % 8));
}
uint32_t readBits(const uint8_t* in, size_t& bit, unsigned width) {
    uint32_t value = 0;
    for (unsigned n = 0; n < width; ++n, ++bit)
        value = (value << 1) | ((in[bit / 8] >> (7 - bit % 8)) & 1U);
    return value;
}
D2Error validate(const D2Frame& f) {
    if (f.event) return D2Error::InvalidFields;
    if (!validDeviceId(f.device)) return D2Error::InvalidDevice;
    if (f.flags & ~0x01FFU) return D2Error::InvalidFlags;
    if (!f.sequence || f.interval < 10 || f.interval > 86400)
        return D2Error::OutOfRange;
    if ((f.flags & 1) ? (f.rtc < 946684800UL || f.rtc > 4102444799UL) : f.rtc != 0)
        return D2Error::FlagValueMismatch;
    if (((f.flags & 32) && !(f.flags & 8)) || ((f.flags & 64) && !(f.flags & 16)) ||
        ((f.flags & 256) && !(f.flags & 128))) return D2Error::FlagDependency;
    for (const auto& s : specs) {
        const auto& v = f.values[s.field];
        if (v.present != ((f.flags & (1U << s.flag)) != 0)) return D2Error::FlagValueMismatch;
        if (v.present && (!isfinite(v.value) || v.value < s.minimum / s.scale ||
                          v.value > s.maximum / s.scale)) return D2Error::OutOfRange;
    }
    return D2Error::Ok;
}
}
D2Error packD2T2(const D2Frame& frame, uint8_t output[D2T2_PAYLOAD_BYTES]) {
    memset(output, 0, D2T2_PAYLOAD_BYTES);
    const auto error = validate(frame);
    if (error != D2Error::Ok) return error;
    size_t bit = 0;
    writeBits(output, bit, frame.sequence, 32); writeBits(output, bit, frame.rtc, 32);
    writeBits(output, bit, frame.uptime, 32); writeBits(output, bit, frame.interval, 17);
    for (const auto& s : specs) {
        int32_t raw = 0;
        if (frame.values[s.field].present) {
            const double scaled = frame.values[s.field].value * s.scale;
            raw = static_cast<int32_t>(scaled >= 0 ? floor(scaled + 0.5) : ceil(scaled - 0.5));
        }
        writeBits(output, bit, static_cast<uint32_t>(raw), s.width);
    }
    writeBits(output, bit, frame.flags, 16); writeBits(output, bit, 0, 6);
    return bit == 480 ? D2Error::Ok : D2Error::InvalidFields;
}
D2Error encodeD2T2(const D2Frame& frame, const uint8_t* key, bool requireAuth,
                   D2Hmac hmac, char output[D2_SMS_CAPACITY]) {
    output[0] = 0;
    uint8_t raw[D2T2_PAYLOAD_BYTES];
    const auto error = packD2T2(frame, raw);
    if (error != D2Error::Ok) return error;
    if (!key && requireAuth) return D2Error::AuthRequired;
    char payload[81]; size_t used = 0;
    for (size_t n = 0; n < sizeof(raw); n += 3) {
        const uint32_t bits = (static_cast<uint32_t>(raw[n]) << 16) |
            (static_cast<uint32_t>(raw[n + 1]) << 8) | raw[n + 2];
        for (int shift = 18; shift >= 0; shift -= 6) payload[used++] = alphabet[(bits >> shift) & 63];
    }
    payload[used] = 0;
    char result[D2_SMS_CAPACITY];
    const int length = snprintf(result, sizeof(result), "D2T2,%s,%s", frame.device, payload);
    if (length < 0 || static_cast<size_t>(length) + 12 >= sizeof(result)) return D2Error::SmsTooLong;
    char tag[12] = "-";
    if (key && !d2MakeTag(key, hmac, result, static_cast<size_t>(length), tag)) return D2Error::CryptoFailure;
    result[length] = ','; strcpy(result + length + 1, tag);
    size_t count = 0; const auto wireError = countD2Septets(result, count);
    if (wireError == D2Error::Ok) strcpy(output, result);
    return wireError;
}
D2Error decodeD2T2(const char* sms, const uint8_t* key, bool requireAuth,
                   D2Hmac hmac, D2Frame& frame) {
    frame = D2Frame{};
    size_t count = 0; const auto wireError = countD2Septets(sms, count);
    if (wireError != D2Error::Ok) return wireError;
    char copy[D2_SMS_CAPACITY]; strcpy(copy, sms);
    char* fields[4] = {copy}; size_t fieldsCount = 1;
    for (char* p = copy; *p; ++p) if (*p == ',') {
        if (fieldsCount == 4) return D2Error::InvalidFields;
        *p = 0; fields[fieldsCount++] = p + 1;
    }
    if (fieldsCount != 4 || strcmp(fields[0], "D2T2")) return D2Error::InvalidFields;
    if (!validDeviceId(fields[1])) return D2Error::InvalidDevice;
    if (strlen(fields[2]) != 80) return D2Error::PayloadShape;
    uint8_t raw[D2T2_PAYLOAD_BYTES] = {}; size_t bit = 0;
    for (const char* p = fields[2]; *p; ++p) {
        const char* digit = strchr(alphabet, *p);
        if (!digit) return D2Error::PayloadShape;
        writeBits(raw, bit, static_cast<uint32_t>(digit - alphabet), 6);
    }
    strcpy(frame.device, fields[1]); bit = 0;
    frame.sequence = readBits(raw, bit, 32); frame.rtc = readBits(raw, bit, 32);
    frame.uptime = readBits(raw, bit, 32); frame.interval = readBits(raw, bit, 17);
    int32_t values[sizeof(specs) / sizeof(specs[0])]; size_t i = 0;
    for (const auto& s : specs) {
        const uint32_t encoded = readBits(raw, bit, s.width);
        values[i++] = s.signedValue && (encoded & (1U << (s.width - 1)))
            ? static_cast<int32_t>(static_cast<int64_t>(encoded) - (INT64_C(1) << s.width))
            : static_cast<int32_t>(encoded);
    }
    frame.flags = static_cast<uint16_t>(readBits(raw, bit, 16));
    if (readBits(raw, bit, 6)) return D2Error::ReservedBits;
    if (!(frame.flags & 1) && frame.rtc) return D2Error::NonCanonicalAbsent;
    i = 0;
    for (const auto& s : specs) {
        const bool present = (frame.flags & (1U << s.flag)) != 0;
        if (!present && values[i] != 0) return D2Error::NonCanonicalAbsent;
        frame.values[s.field] = {values[i++] / s.scale, present};
    }
    const auto error = validate(frame); if (error != D2Error::Ok) return error;
    const char* auth = fields[3];
    if (!strcmp(auth, "-")) return requireAuth ? D2Error::AuthRequired : D2Error::Ok;
    if (!key) return D2Error::AuthRequired;
    if (strlen(auth) != 11) return D2Error::AuthInvalid;
    char expected[12];
    const size_t signedLength = static_cast<size_t>(strrchr(sms, ',') - sms);
    if (!d2MakeTag(key, hmac, sms, signedLength, expected)) return D2Error::CryptoFailure;
    uint8_t mismatch = 0;
    for (size_t n = 0; n < 11; ++n) mismatch |= static_cast<uint8_t>(auth[n] ^ expected[n]);
    return mismatch ? D2Error::AuthInvalid : D2Error::Ok;
}
}
