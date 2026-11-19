#include "d3_codec.h"
#include <stdio.h>
#include <string.h>
#include <initializer_list>
namespace djua {
namespace {
constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
struct Writer {
    uint8_t* bytes; size_t bit = 0;
    void put(uint32_t value, unsigned width) {
        for (unsigned n = width; n > 0; --n, ++bit)
            if ((value >> (n - 1)) & 1U) bytes[bit / 8] |= static_cast<uint8_t>(1U << (7 - bit % 8));
    }
    void number(double value, double scale, unsigned width) {
        const double raw = value * scale;
        put(static_cast<uint32_t>(static_cast<int32_t>(raw < 0 ? ceil(raw - .5) : floor(raw + .5))), width);
    }
};
struct Reader {
    const uint8_t* bytes; size_t bit = 0;
    uint32_t get(unsigned width) {
        uint32_t value = 0;
        for (unsigned i = 0; i < width; ++i, ++bit) value = (value << 1) | ((bytes[bit / 8] >> (7 - bit % 8)) & 1U);
        return value;
    }
    double number(double scale, unsigned width, bool signedValue = false) {
        const uint32_t raw = get(width);
        int64_t value = raw;
        if (signedValue && (raw & (1U << (width - 1)))) value -= INT64_C(1) << width;
        return value / scale;
    }
};
bool range(double value, double minimum, double maximum) {
    return isfinite(value) && value >= minimum && value <= maximum;
}
D2Error slotError(const ElectricalSlot& s, uint32_t interval, bool ac = false) {
    if (static_cast<unsigned>(s.fault) > static_cast<unsigned>(MeasureFault::CounterReset)) return D2Error::InvalidFlags;
    if (s.valid != (s.fault == MeasureFault::None) || (s.energyComplete && !s.valid)) return D2Error::FlagDependency;
    if (!s.valid && (s.voltage != 0 || s.current != 0 || s.power != 0)) return D2Error::NonCanonicalAbsent;
    if (!s.energyComplete && s.energyWh != 0) return D2Error::NonCanonicalAbsent;
    if (s.valid && (!range(s.voltage, 0, ac ? 260 : 85) || !range(s.current, ac ? 0 : -30, ac ? 10 : 30) ||
        !range(s.power, ac ? 0 : -3000, ac ? 2300 : 3000))) return D2Error::OutOfRange;
    if (s.valid && !ac && fabs(s.power - s.voltage * s.current) > 1 + fabs(s.power) * .02)
        return D2Error::OutOfRange;
    if (s.energyComplete && !range(s.energyWh, ac ? 0 : -3000.0 * interval / 3600 - 1,
        (ac ? 2300.0 : 3000.0) * interval / 3600 + 1)) return D2Error::OutOfRange;
    return D2Error::Ok;
}
D2Error validate(const TelemetryV3& f) {
    if (!validDeviceId(f.device)) return D2Error::InvalidDevice;
    if (f.part > 1 || !f.snapshot || f.snapshot == UINT32_MAX || f.sequence != f.snapshot + f.part ||
        f.interval < 10 || f.interval > 86400) return D2Error::OutOfRange;
    if (f.rtcValid ? (f.rtc < 946684800UL || f.rtc > 4102444799UL) : f.rtc != 0) return D2Error::FlagValueMismatch;
    if (f.part == 1) {
        for (const auto& s : f.dc) { const auto error = slotError(s, f.interval); if (error != D2Error::Ok) return error; }
        return D2Error::Ok;
    }
    const auto& l = f.location;
    if (static_cast<unsigned>(l.source) > 2 || (l.valid && l.source == LocationSource::None) ||
        (l.accuracyKnown && !l.valid)) return D2Error::FlagDependency;
    if (!l.valid && (l.latitude != 0 || l.longitude != 0)) return D2Error::NonCanonicalAbsent;
    if (!l.accuracyKnown && l.accuracyM != 0) return D2Error::NonCanonicalAbsent;
    if (!range(l.latitude, -90, 90) || !range(l.longitude, -180, 180) ||
        !range(l.accuracyM, 0, 65534)) return D2Error::OutOfRange;
    for (const auto* s : {&f.battery, &f.solar, &f.ac.electric}) {
        const auto error = slotError(*s, f.interval, s == &f.ac.electric);
        if (error != D2Error::Ok) return error;
    }
    const auto& a = f.ac;
    if ((a.counterValid && !a.electric.valid) || (a.electric.energyComplete &&
        (!a.counterValid || a.energyFault != MeasureFault::None)) || static_cast<unsigned>(a.energyFault) > 12)
        return D2Error::FlagDependency;
    if ((!a.counterValid && a.counterWh) || (!a.electric.valid &&
        (a.apparentVA != 0 || a.frequencyHz != 0 || a.powerFactor != 0 || a.alarm))) return D2Error::NonCanonicalAbsent;
    if (a.counterWh > 9999990 || !range(a.apparentVA, 0, 2600) || !range(a.frequencyHz, 0, 65) ||
        !range(a.powerFactor, 0, 1) || a.electric.power > a.apparentVA + 2 + a.apparentVA * .03)
        return D2Error::OutOfRange;
    return D2Error::Ok;
}
void writeSlot(Writer& w, const ElectricalSlot& s) {
    w.put(s.valid, 1); w.put(s.energyComplete, 1); w.put(static_cast<uint32_t>(s.fault), 4); w.put(s.ageMs, 16);
    w.number(s.voltage, 1000, 17); w.number(s.current, 1000, 17); w.number(s.power, 10, 20); w.number(s.energyWh, 100, 27);
}
void readSlot(Reader& r, ElectricalSlot& s) {
    s.valid = r.get(1); s.energyComplete = r.get(1); s.fault = static_cast<MeasureFault>(r.get(4)); s.ageMs = static_cast<uint16_t>(r.get(16));
    s.voltage = r.number(1000, 17); s.current = r.number(1000, 17, true);
    s.power = r.number(10, 20, true); s.energyWh = r.number(100, 27, true);
}
}
D2Error packD3(const TelemetryV3& f, uint8_t output[D3_PAYLOAD_BYTES]) {
    memset(output, 0, D3_PAYLOAD_BYTES);
    auto error = validate(f); if (error != D2Error::Ok) return error;
    Writer w{output};
    w.put(f.sequence, 32); w.put(f.snapshot, 32); w.put(f.rtc, 32); w.put(f.uptime, 32);
    w.put(f.interval, 17); w.put(f.part, 1); w.put(f.rtcValid, 1);
    if (f.part) { for (const auto& s : f.dc) writeSlot(w, s); }
    else {
        const auto& l = f.location;
        w.put(static_cast<uint32_t>(l.source), 2); w.put(l.valid, 1); w.put(l.accuracyKnown, 1);
        w.put(boundedAge(l.ageMs), 16); w.number(l.accuracyM, 1, 16);
        w.number(l.latitude, 100000, 25); w.number(l.longitude, 100000, 26);
        writeSlot(w, f.battery); writeSlot(w, f.solar);
        const auto& a = f.ac; const auto& s = a.electric;
        w.put(s.valid, 1); w.put(s.energyComplete, 1); w.put(a.counterValid, 1); w.put(a.alarm, 1);
        w.put(static_cast<uint32_t>(s.fault), 4); w.put(static_cast<uint32_t>(a.energyFault), 4); w.put(s.ageMs, 16);
        w.number(s.voltage, 10, 12); w.number(s.current, 1000, 14); w.number(s.power, 10, 15);
        w.number(a.apparentVA, 10, 15); w.number(s.energyWh, 100, 27); w.put(a.counterWh, 24);
        w.number(a.frequencyHz, 10, 10); w.number(a.powerFactor, 100, 7);
    }
    return w.bit <= D3_PAYLOAD_BYTES * 8 ? D2Error::Ok : D2Error::InvalidFields;
}
D2Error encodeD3(const TelemetryV3& f, const uint8_t* key, bool requireAuth, D2Hmac hmac, char output[D2_SMS_CAPACITY]) {
    output[0] = 0; uint8_t raw[D3_PAYLOAD_BYTES];
    auto error = packD3(f, raw); if (error != D2Error::Ok) return error;
    if (!key && requireAuth) return D2Error::AuthRequired;
    char payload[101]; size_t n = 0;
    for (size_t i = 0; i < sizeof(raw); i += 3) {
        uint32_t bits = (static_cast<uint32_t>(raw[i]) << 16) | (static_cast<uint32_t>(raw[i + 1]) << 8) | raw[i + 2];
        for (int shift = 18; shift >= 0; shift -= 6) payload[n++] = alphabet[(bits >> shift) & 63];
    }
    payload[n] = 0;
    char text[D2_SMS_CAPACITY]; int size = snprintf(text, sizeof(text), "D3T,%s,%s", f.device, payload);
    if (size < 0 || static_cast<size_t>(size) + 12 >= sizeof(text)) return D2Error::SmsTooLong;
    char tag[12] = "-";
    if (key && !d2MakeTag(key, hmac, text, static_cast<size_t>(size), tag)) return D2Error::CryptoFailure;
    text[size] = ','; strcpy(text + size + 1, tag);
    size_t septets = 0; error = countD2Septets(text, septets);
    if (error == D2Error::Ok) strcpy(output, text);
    return error;
}
D2Error decodeD3(const char* sms, const uint8_t* key, bool requireAuth, D2Hmac hmac, TelemetryV3& f) {
    f = TelemetryV3{};
    size_t septets = 0; auto error = countD2Septets(sms, septets); if (error != D2Error::Ok) return error;
    char copy[D2_SMS_CAPACITY]; strcpy(copy, sms); char* fields[4] = {copy}; size_t count = 1;
    for (char* p = copy; *p; ++p) if (*p == ',') { if (count == 4) return D2Error::InvalidFields; *p = 0; fields[count++] = p + 1; }
    if (count != 4 || strcmp(fields[0], "D3T")) return D2Error::InvalidFields;
    if (!validDeviceId(fields[1])) return D2Error::InvalidDevice;
    if (strlen(fields[2]) != 100) return D2Error::PayloadShape;
    uint8_t raw[D3_PAYLOAD_BYTES] = {}; Writer w{raw};
    for (const char* p = fields[2]; *p; ++p) { const char* digit = strchr(alphabet, *p);
        if (!digit) return D2Error::PayloadShape; w.put(static_cast<uint32_t>(digit - alphabet), 6); }
    Reader r{raw}; strcpy(f.device, fields[1]); f.sequence = r.get(32); f.snapshot = r.get(32);
    f.rtc = r.get(32); f.uptime = r.get(32); f.interval = r.get(17);
    f.part = static_cast<uint8_t>(r.get(1)); f.rtcValid = r.get(1);
    if (f.part) { for (auto& s : f.dc) readSlot(r, s); }
    else {
        auto& l = f.location; l.source = static_cast<LocationSource>(r.get(2));
        l.valid = r.get(1); l.accuracyKnown = r.get(1); l.ageMs = r.get(16); l.accuracyM = r.number(1, 16);
        l.latitude = r.number(100000, 25, true); l.longitude = r.number(100000, 26, true);
        readSlot(r, f.battery); readSlot(r, f.solar);
        auto& a = f.ac; auto& s = a.electric;
        s.valid = r.get(1); s.energyComplete = r.get(1); a.counterValid = r.get(1); a.alarm = r.get(1);
        s.fault = static_cast<MeasureFault>(r.get(4)); a.energyFault = static_cast<MeasureFault>(r.get(4)); s.ageMs = static_cast<uint16_t>(r.get(16));
        s.voltage = r.number(10, 12); s.current = r.number(1000, 14); s.power = r.number(10, 15);
        a.apparentVA = r.number(10, 15); s.energyWh = r.number(100, 27); a.counterWh = r.get(24);
        a.frequencyHz = r.number(10, 10); a.powerFactor = r.number(100, 7);
    }
    while (r.bit < D3_PAYLOAD_BYTES * 8) if (r.get(1)) return D2Error::ReservedBits;
    error = validate(f); if (error != D2Error::Ok) return error;
    if (!strcmp(fields[3], "-")) return requireAuth ? D2Error::AuthRequired : D2Error::Ok;
    if (!key) return D2Error::AuthRequired;
    if (strlen(fields[3]) != 11) return D2Error::AuthInvalid;
    char expected[12];
    if (!d2MakeTag(key, hmac, sms, static_cast<size_t>(strrchr(sms, ',') - sms), expected)) return D2Error::CryptoFailure;
    uint8_t mismatch = 0;
    for (size_t i = 0; i < 11; ++i) mismatch |= static_cast<uint8_t>(expected[i] ^ fields[3][i]);
    return mismatch ? D2Error::AuthInvalid : D2Error::Ok;
}
}
