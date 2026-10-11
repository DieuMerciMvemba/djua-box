#include "d2_codec.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace djua {
namespace {
struct Spec { double scale; int64_t min, max; uint8_t bit; };
constexpr Spec specs[D2_LEGACY_FIELD_COUNT] = {
    {100000, -9000000, 9000000, 1}, {100000, -18000000, 18000000, 1},
    {1000, 0, 32000, 2}, {1000, -200000, 200000, 2}, {10, -64000, 64000, 2},
    {1000, 0, 100000, 3}, {1000, -100000, 100000, 3}, {10, -100000, 100000, 3},
    {100, -24000000, 24000000, 5}, {10, 0, 4000, 4}, {100, 0, 3000, 4},
    {10, -120000, 120000, 4}, {10, 0, 120000, 4}, {100, -28800000, 28800000, 6},
    {1, 0, 1000000, 2}
};
constexpr char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr char base64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

void base36(int64_t number, char* output, bool fixed = false) {
    char reverse[16]; size_t n = 0, used = 0;
    uint64_t magnitude = static_cast<uint64_t>(number < 0 ? -number : number);
    do { reverse[n++] = digits[magnitude % 36]; magnitude /= 36; } while (magnitude);
    if (number < 0) output[used++] = '-';
    if (fixed && n == 1) output[used++] = '0';
    while (n) output[used++] = reverse[--n];
    output[used] = 0;
}
D2Error number(const char* token, int64_t min, int64_t max, int64_t& result,
               bool fixed = false) {
    const size_t length = strlen(token);
    if (!length || (fixed && length != 2)) return D2Error::NonCanonicalBase36;
    bool negative = token[0] == '-';
    const char* p = token + (negative ? 1 : 0);
    if (!*p || (negative && min >= 0) || (fixed && negative) ||
        (!fixed && p[0] == '0' && p[1]) || (negative && p[0] == '0'))
        return D2Error::NonCanonicalBase36;
    uint64_t value = 0;
    for (; *p; ++p) {
        const char* digit = strchr(digits, *p);
        if (!digit) return D2Error::NonCanonicalBase36;
        const uint64_t d = static_cast<uint64_t>(digit - digits);
        if (value > (UINT64_C(4294967295) - d) / 36) return D2Error::OutOfRange;
        value = value * 36 + d;
    }
    result = negative ? -static_cast<int64_t>(value) : static_cast<int64_t>(value);
    return result < min || result > max ? D2Error::OutOfRange : D2Error::Ok;
}
bool fieldExpected(const D2Frame& frame, size_t i) {
    const uint8_t bit = frame.event ? (i == Lat || i == Lon ? 1 : 2) : specs[i].bit;
    return (frame.flags & (1U << bit)) != 0;
}
D2Error coherence(const D2Frame& frame) {
    if (!validDeviceId(frame.device)) return D2Error::InvalidDevice;
    if (!frame.sequence || (!frame.event && (frame.interval < 10 || frame.interval > 86400)))
        return D2Error::OutOfRange;
    if (frame.flags > (frame.event ? 7 : 127)) return D2Error::OutOfRange;
    if ((frame.flags & 1) ? (frame.rtc < 946684800UL || frame.rtc > 4102444799UL)
                          : frame.rtc != 0) return D2Error::FlagValueMismatch;
    if (frame.event ? ((frame.flags & 4) && !(frame.flags & 2))
                    : (((frame.flags & 32) && !(frame.flags & 8)) ||
                       ((frame.flags & 64) && !(frame.flags & 16))))
        return D2Error::FlagValueMismatch;
    for (size_t i = 0; i < D2_LEGACY_FIELD_COUNT; ++i) {
        const bool transported = frame.event ? (i == Lat || i == Lon || i == Distance) : i != Distance;
        if (!transported) continue;
        const auto& value = frame.values[i];
        if (value.present != fieldExpected(frame, i)) return D2Error::FlagValueMismatch;
        if (value.present && (!isfinite(value.value) ||
            value.value < specs[i].min / specs[i].scale ||
            value.value > specs[i].max / specs[i].scale)) return D2Error::OutOfRange;
    }
    return D2Error::Ok;
}
}
bool d2MakeTag(const uint8_t* key, D2Hmac hmac, const char* data, size_t length, char tag[12]) {
    uint8_t digest[32] = {};
    if (!hmac || !hmac(key, data, length, digest)) return false;
    uint32_t bits = 0; int available = 0; size_t used = 0;
    for (size_t i = 0; i < 8; ++i) {
        bits = (bits << 8) | digest[i]; available += 8;
        while (available >= 6) { available -= 6; tag[used++] = base64[(bits >> available) & 63]; }
    }
    if (available) tag[used++] = base64[(bits << (6 - available)) & 63];
    tag[used] = 0;
    memset(digest, 0, sizeof(digest));
    return used == 11;
}
namespace {
bool append(char* out, const char* token, bool comma = true) {
    const size_t used = strlen(out), length = strlen(token);
    if (used + length + (comma ? 1 : 0) >= D2_SMS_CAPACITY) return false;
    if (comma) out[used] = ',';
    memcpy(out + used + (comma ? 1 : 0), token, length + 1);
    return true;
}
}
bool validDeviceId(const char* id) {
    if (!id) return false;
    size_t n = 0;
    for (; n < 33 && id[n]; ++n)
        if (!(id[n] >= 'A' && id[n] <= 'Z') && !(id[n] >= '0' && id[n] <= '9') && id[n] != '-')
            return false;
    return n >= 1 && n <= 32;
}
bool validE164(const char* number) {
    if (!number || number[0] != '+' || number[1] < '1' || number[1] > '9') return false;
    size_t n = 1;
    for (; n <= 16 && number[n]; ++n) if (number[n] < '0' || number[n] > '9') return false;
    return n >= 2 && n <= 16;
}
D2Error countD2Septets(const char* sms, size_t& count) {
    count = 0;
    if (!sms) return D2Error::InvalidFields;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(sms); *p; ++p) {
        // Toutes ces entrees appartiennent a la table GSM-7 de base : 1 septet.
        // Toute extension (2 septets), commande ou conversion UCS-2 est refusee.
        if (!(*p >= 'A' && *p <= 'Z') && !(*p >= 'a' && *p <= 'z') &&
            !(*p >= '0' && *p <= '9') && *p != ',' && *p != '-' && *p != '_')
            return D2Error::NonGsm7;
        ++count;
    }
    return count > 160 ? D2Error::SmsTooLong : D2Error::Ok;
}
D2Error encodeD2(const D2Frame& frame, const uint8_t* key, bool requireAuth,
                 D2Hmac hmac, char output[D2_SMS_CAPACITY]) {
    output[0] = 0;
    D2Error error = coherence(frame);
    if (error != D2Error::Ok) return error;
    if (!key && requireAuth) return D2Error::AuthRequired;
    char result[D2_SMS_CAPACITY] = {}, token[20];
    append(result, frame.event ? "D2E" : "D2T", false);
    append(result, frame.device);
    base36(frame.sequence, token); append(result, token);
    if (frame.flags & 1) base36(frame.rtc, token); else strcpy(token, "-");
    append(result, token);
    base36(frame.uptime, token); append(result, token);
    if (frame.event) append(result, frame.exit ? "GX" : "GE");
    else { base36(frame.interval, token); append(result, token); }
    for (size_t i = 0; i < D2_LEGACY_FIELD_COUNT; ++i) {
        if (frame.event ? (i != Lat && i != Lon && i != Distance) : i == Distance) continue;
        if (!frame.values[i].present) strcpy(token, "-");
        else {
            const double scaled = frame.values[i].value * specs[i].scale;
            const int64_t integer = static_cast<int64_t>(scaled >= 0 ? floor(scaled + 0.5) : ceil(scaled - 0.5));
            base36(integer, token);
        }
        if (!append(result, token)) return D2Error::SmsTooLong;
    }
    base36(frame.flags, token, true);
    if (!append(result, token)) return D2Error::SmsTooLong;
    char tag[12] = "-";
    if (key && !d2MakeTag(key, hmac, result, strlen(result), tag)) return D2Error::CryptoFailure;
    if (!append(result, tag)) return D2Error::SmsTooLong;
    size_t septets = 0; error = countD2Septets(result, septets);
    if (error == D2Error::Ok) strcpy(output, result);
    return error;
}
D2Error decodeD2(const char* sms, const uint8_t* key, bool requireAuth,
                 D2Hmac hmac, D2Frame& frame) {
    if (sms && !strncmp(sms, "D2T2,", 5)) return decodeD2T2(sms, key, requireAuth, hmac, frame);
    frame = D2Frame{};
    size_t septets = 0; D2Error error = countD2Septets(sms, septets);
    if (error != D2Error::Ok) return error;
    char copy[D2_SMS_CAPACITY]; strcpy(copy, sms);
    char* fields[22] = {}; size_t count = 1; fields[0] = copy;
    for (char* p = copy; *p; ++p) if (*p == ',') {
        *p = 0;
        if (count >= 22) return D2Error::InvalidFields;
        fields[count++] = p + 1;
    }
    frame.event = strcmp(fields[0], "D2E") == 0;
    if ((!frame.event && strcmp(fields[0], "D2T")) || count != (frame.event ? 11U : 22U))
        return D2Error::InvalidFields;
    if (!validDeviceId(fields[1])) return D2Error::InvalidDevice;
    strcpy(frame.device, fields[1]);
    int64_t value = 0;
    error = number(fields[2], 1, UINT32_MAX, value); if (error != D2Error::Ok) return error;
    frame.sequence = static_cast<uint32_t>(value);
    const bool rtcPresent = strcmp(fields[3], "-") != 0;
    if (rtcPresent) {
        error = number(fields[3], 946684800, 4102444799LL, value); if (error != D2Error::Ok) return error;
        frame.rtc = static_cast<uint32_t>(value);
    }
    error = number(fields[4], 0, UINT32_MAX, value); if (error != D2Error::Ok) return error;
    frame.uptime = static_cast<uint32_t>(value);
    if (frame.event) {
        if (strcmp(fields[5], "GX") && strcmp(fields[5], "GE")) return D2Error::InvalidFields;
        frame.exit = strcmp(fields[5], "GX") == 0;
    } else {
        error = number(fields[5], 10, 86400, value); if (error != D2Error::Ok) return error;
        frame.interval = static_cast<uint32_t>(value);
    }
    error = number(fields[count - 2], 0, frame.event ? 7 : 127, value, true);
    if (error != D2Error::Ok) return error;
    frame.flags = static_cast<uint8_t>(value);
    if (rtcPresent != ((frame.flags & 1) != 0)) return D2Error::FlagValueMismatch;
    size_t f = 6;
    for (size_t i = 0; i < D2_LEGACY_FIELD_COUNT; ++i) {
        if (frame.event ? (i != Lat && i != Lon && i != Distance) : i == Distance) continue;
        if (strcmp(fields[f], "-")) {
            error = number(fields[f], specs[i].min, specs[i].max, value);
            if (error != D2Error::Ok) return error;
            frame.values[i] = {value / specs[i].scale, true};
        }
        ++f;
    }
    error = coherence(frame); if (error != D2Error::Ok) return error;
    const char* auth = fields[count - 1];
    if (!strcmp(auth, "-")) return requireAuth ? D2Error::AuthRequired : D2Error::Ok;
    if (!key) return D2Error::AuthRequired;
    if (strlen(auth) != 11) return D2Error::AuthInvalid;
    char expected[12];
    const size_t signedLength = static_cast<size_t>(strrchr(sms, ',') - sms);
    if (!d2MakeTag(key, hmac, sms, signedLength, expected)) return D2Error::CryptoFailure;
    uint8_t mismatch = 0;
    for (size_t i = 0; i < 11; ++i) mismatch |= static_cast<uint8_t>(auth[i] ^ expected[i]);
    return mismatch ? D2Error::AuthInvalid : D2Error::Ok;
}
const char* d2ErrorName(D2Error error) {
    switch (error) {
    case D2Error::Ok: return "OK";
    case D2Error::NonGsm7: return "NON_GSM7";
    case D2Error::SmsTooLong: return "SMS_TOO_LONG";
    case D2Error::InvalidFields: return "INVALID_FIELDS";
    case D2Error::InvalidDevice: return "INVALID_DEVICE";
    case D2Error::NonCanonicalBase36: return "NON_CANONICAL_BASE36";
    case D2Error::OutOfRange: return "OUT_OF_RANGE";
    case D2Error::FlagValueMismatch: return "FLAG_VALUE_MISMATCH";
    case D2Error::AuthRequired: return "AUTH_REQUIRED";
    case D2Error::AuthInvalid: return "AUTH_INVALID";
    case D2Error::CryptoFailure: return "CRYPTO_FAILURE";
    case D2Error::InvalidFlags: return "INVALID_FLAGS";
    case D2Error::NonCanonicalAbsent: return "NON_CANONICAL_ABSENT";
    case D2Error::FlagDependency: return "FLAG_DEPENDENCY";
    case D2Error::PayloadShape: return "D2T2_PAYLOAD_SHAPE";
    case D2Error::ReservedBits: return "D2T2_RESERVED_BITS";
    }
    return "UNKNOWN";
}
bool d2UtcEpoch(uint16_t year, uint8_t month, uint8_t day, uint8_t hour,
                uint8_t minute, uint8_t second, int offsetMinutes, uint32_t& epoch) {
    epoch = 0;
    if (year < 2000 || year > 2100 || month < 1 || month > 12 ||
        hour > 23 || minute > 59 || second > 59 || offsetMinutes != 60) return false;
    const uint8_t lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    const auto leap = [](uint16_t y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); };
    const int maxDay = lengths[month - 1] + (month == 2 && leap(year) ? 1 : 0);
    if (day < 1 || day > maxDay) return false;
    int64_t days = 0;
    for (uint16_t y = 2000; y < year; ++y) days += leap(y) ? 366 : 365;
    for (uint8_t m = 1; m < month; ++m) days += lengths[m - 1] + (m == 2 && leap(year) ? 1 : 0);
    days += day - 1;
    const int64_t utc = 946684800LL + days * 86400 + hour * 3600 + minute * 60 + second - offsetMinutes * 60;
    if (utc < 946684800 || utc > 4102444799LL) return false;
    epoch = static_cast<uint32_t>(utc); return true;
}
}
