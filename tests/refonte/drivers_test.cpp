#include "../../src/drivers/ina228.h"
#include "../../src/drivers/ina3221.h"
#include "../../src/drivers/pzem004t.h"
#include "../../src/communication/gsm.h"
#include "../../src/location/location.h"
#include "../../src/communication/d2_store.h"
#include "../sms/host_crypto.h"
#undef near // Windows legacy calling-convention macro, used only by host BCrypt.
#include "../../src/telemetry/energy_window.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <utility>
#include <deque>
#include <vector>
#include <string>
#include <cstring>
using namespace djua;

struct Registers : I2cPort {
    std::map<std::pair<uint8_t, uint8_t>, uint32_t> values;
    int failReg = -1;
    bool transfer(uint8_t address, const uint8_t* tx, size_t n, uint8_t* rx, size_t size) override {
        if (tx[0] == failReg) return false;
        auto key = std::make_pair(address, tx[0]);
        if (n == 3) values[key] = (static_cast<uint32_t>(tx[1]) << 8) | tx[2];
        else for (size_t i = 0; i < size; ++i)
            rx[i] = static_cast<uint8_t>(values[key] >> (8 * (size - i - 1)));
        return true;
    }
    uint32_t& reg(uint8_t address, uint8_t reg) { return values[{address, reg}]; }
    void ina228(uint8_t address) {
        reg(address, 0x3e) = 0x5449; reg(address, 0x3f) = 0x2281;
        reg(address, 0x0b) = 3;
        reg(address, 5) = 61440U << 4; // 12 V
        reg(address, 4) = 80000U << 4; // 25 mV -> 10 A
        reg(address, 7) = 174763U << 4;
    }
};
static bool near(double a, double b, double delta = .002) { return fabs(a - b) < delta; }
struct SerialFake : ByteStream {
    std::deque<uint8_t> rx;
    std::vector<uint8_t> tx;
    int available() override { return static_cast<int>(rx.size()); }
    int read() override { if (rx.empty()) return -1; int c = rx.front(); rx.pop_front(); return c; }
    bool write(const uint8_t* p, size_t n) override { tx.insert(tx.end(), p, p + n); return true; }
    void response(bool corrupt = false, uint16_t current = 1200) {
        const uint16_t registers[10] = {2300, current, 0, 2300, 0, 1000, 0, 500, 83, 0};
        uint8_t bytes[25] = {0xf8, 4, 20};
        for (unsigned i = 0; i < 10; ++i) {
            bytes[3 + 2 * i] = static_cast<uint8_t>(registers[i] >> 8);
            bytes[4 + 2 * i] = static_cast<uint8_t>(registers[i]);
        }
        const uint16_t crc = modbusCrc(bytes, 23);
        bytes[23] = static_cast<uint8_t>(crc ^ (corrupt ? 1 : 0));
        bytes[24] = static_cast<uint8_t>(crc >> 8);
        for (auto byte : bytes) rx.push_back(byte);
    }
};
static void pzemTests() {
    SerialFake serial; Pzem004t meter(serial);
    assert(meter.start(0) && !meter.start(1));
    assert(serial.tx.size() == 8 && serial.tx[0] == 0xf8 && serial.tx[1] == 4);
    assert(modbusCrc(serial.tx.data(), serial.tx.size()) == 0);
    serial.response(); meter.update(30);
    auto value = meter.reading(30);
    assert(value.electric.valid && near(value.electric.power, 230));
    assert(near(value.apparentVA, 276) && near(value.frequencyHz, 50));
    assert(value.counterWh == 1000 && value.counterValid);
    assert(!meter.reading(5031).electric.valid);
    assert(meter.start(10000)); serial.response(true); meter.update(10030);
    assert(meter.reading(10030).electric.fault == MeasureFault::Crc);
    assert(meter.start(20000)); serial.response(false, 10001); meter.update(20030);
    assert(!meter.reading(20030).electric.valid);
    assert(meter.start(UINT32_MAX - 499)); meter.update(500);
    assert(meter.reading(500).electric.fault == MeasureFault::Timeout);
    assert(meter.start(1000));
    serial.rx = {0xf8, 0x84, 2};
    uint8_t exception[3] = {0xf8, 0x84, 2};
    const uint16_t crc = modbusCrc(exception, 3);
    serial.rx.push_back(static_cast<uint8_t>(crc)); serial.rx.push_back(static_cast<uint8_t>(crc >> 8));
    meter.update(1001); assert(meter.reading(1001).electric.fault == MeasureFault::Protocol);
    CounterWindow counter; counter.sample(0, 1000, true); counter.sample(10000, 1001, true);
    assert(counter.complete(10000) && counter.wattHours() == 1);
    counter.sample(20000, 0, true);
    assert(!counter.complete(20000) && counter.fault() == MeasureFault::CounterReset);
    counter.reset(20000, 0, true); counter.sample(30000, 1, false);
    counter.sample(40000, 2, true); assert(!counter.complete(40000));
    counter.reset(40000, 2, true); counter.sample(50000, 100, true);
    assert(!counter.complete(50000) && counter.fault() == MeasureFault::Range);
    puts("PASS PZEM: Modbus/CRC, six readings, 10 A limits, stale, timeout, exceptions, counter reset/gap/jump");
}
struct AtFake : GsmIo {
    std::deque<char> rx; std::string tx;
    int available() override { return static_cast<int>(rx.size()); }
    int read() override { int c = rx.front(); rx.pop_front(); return c; }
    bool write(const char* p, size_t n) override { tx.append(p, n); return true; }
    void feed(const std::string& text) { for (char c : text) rx.push_back(c); }
};
static void initLte(GsmEngine& engine, AtFake& io, bool supported = true, bool centre = true) {
    engine.begin(true, 0); engine.update(0);
    const char* responses[] = {"OK", "OK", "OK", "+CPIN: READY\r\nOK",
        "+CEREG: 0,1\r\nOK", "+CREG: 0,0\r\nOK", "+CSMS: 0,1,1,0\r\nOK",
        "+CSCA: \"+243810000000\",145\r\nOK", "OK", "OK", "OK"};
    for (unsigned i = 0; i < 11 && engine.state() != GsmState::Backoff; ++i) {
        io.feed(std::string(i == 6 && !supported ? "+CSMS: 0,1,0,0\r\nOK" :
            i == 7 && !centre ? "+CSCA: \"\",145\r\nOK" : responses[i]) + "\r\n");
        engine.update(i + 1);
    }
}
struct FixProvider : LocationProvider {
    LocationFix fix;
    void update() override {}
    LocationFix read() override { return fix; }
};
static void modemAndLocationTests() {
    AtFake io; GsmEngine lte(io, 10000, 120000, 30000, GsmModel::A7670);
    initLte(lte, io); assert(lte.ready() && lte.lteRegistered() && lte.smsConfigured());
    assert(io.tx.find("AT+CEREG?") != std::string::npos);
    assert(io.tx.find("AT+CGNS") == std::string::npos);
    assert(lte.submit("+243810000001", "D2E,A,1,-,0,GX,-,-,-,00,-", 20));
    io.feed(">"); lte.update(21); io.feed("+CMS ERROR: 500\r\n"); lte.update(22);
    assert(lte.takeResult() == GsmResult::Failed && lte.errorCode() == 500);
    AtFake noSms; GsmEngine blocked(noSms, 10000, 120000, 30000, GsmModel::A7670);
    initLte(blocked, noSms, false);
    assert(!blocked.ready() && blocked.fault() == GsmFault::SmsUnsupported);
    AtFake noCentre; GsmEngine missing(noCentre, 10000, 120000, 30000, GsmModel::A7670);
    initLte(missing, noCentre, true, false);
    assert(!missing.ready() && missing.fault() == GsmFault::ServiceCentreMissing);
    initLte(lte, io); io.feed("+CEREG: 5,\"001A\",\"00001234\",7\r\n"); lte.update(30);
    assert(lte.lteRegistered());
    io.feed("+CEREG: 0\r\n"); lte.update(31); assert(!lte.ready());
    assert(!lte.submit("+243810000001", "D2E,A,1,-,0,GX,-,-,-,00,-", 31));
    FixProvider gnss, cell;
    gnss.fix = {0, 0, true};
    cell.fix = {5, 6, true}; cell.fix.source = LocationSource::Cellular;
    cell.fix.accuracyKnown = true; cell.fix.accuracyM = 1000;
    LocationService service(gnss, &cell);
    assert(service.read(5000).source == LocationSource::NeoGnss);
    gnss.fix.ageMs = 5001;
    auto fix = service.read(5000);
    assert(fix.valid && fix.source == LocationSource::Cellular && !precisionGeofenceUsable(fix, 5000, 10));
    cell.fix.valid = false; assert(!service.read(5000).valid);
    gnss.fix.ageMs = 0; gnss.fix.accuracyKnown = true; gnss.fix.accuracyM = 100;
    assert(!precisionGeofenceUsable(gnss.fix, 5000, 10));
    puts("PASS A7670/location: LTE without SMS, SMSC, CMS error, registration loss, GNSS priority, freshness, cell geofence rejected");
}
struct TestStore : D2Store {
    std::map<std::string, std::vector<uint8_t>> records;
    int writes = 0, failAt = -1;
    ReadResult read(const char* name, void* out, size_t size) override {
        auto entry = records.find(name); if (entry == records.end()) return ReadResult::Missing;
        if (entry->second.size() != size) return ReadResult::Error;
        memcpy(out, entry->second.data(), size); return ReadResult::Ok;
    }
    bool write(const char* name, const void* data, size_t size) override {
        if (++writes == failAt) return false;
        const uint8_t* p = static_cast<const uint8_t*>(data); records[name] = {p, p + size}; return true;
    }
};
static uint8_t publicTestKey[32];
static TelemetryV3 vectorFrame() {
    TelemetryV3 frame;
    strcpy(frame.device, "ABCDEFGHIJKLMNOPQRSTUVWXYZ123456");
    frame.sequence = frame.snapshot = 1; frame.uptime = 123456; frame.interval = 1800;
    frame.location = {-4.33086, 15.30678, true}; frame.location.ageMs = 1000;
    frame.battery = {12.543, -1.234, -15.5, -4.56, true, true, MeasureFault::None, 2};
    frame.solar = {18.2, 2, 36.4, 18.2, true, true, MeasureFault::None, 3};
    frame.ac.electric = {230, 1.2, 230, 115, true, true, MeasureFault::None, 4};
    frame.ac.apparentVA = 276; frame.ac.powerFactor = .83; frame.ac.frequencyHz = 50;
    frame.ac.counterWh = 1000; frame.ac.counterValid = true; frame.ac.energyFault = MeasureFault::None;
    for (auto& slot : frame.dc) slot = {12, 2, 24, 12, true, true, MeasureFault::None, 5};
    frame.dc[2] = {}; frame.dc[2].fault = MeasureFault::Io;
    return frame;
}
static void codecTests() {
    auto frame = vectorFrame(); char sms[D2_SMS_CAPACITY]; TelemetryV3 parsed;
    assert(encodeD3(frame, publicTestKey, true, hostHmac, sms) == D2Error::Ok);
    size_t length = 0; assert(countD2Septets(sms, length) == D2Error::Ok && length == 149);
    assert(decodeD3(sms, publicTestKey, true, hostHmac, parsed) == D2Error::Ok);
    assert(parsed.part == 0 && near(parsed.battery.current, -1.234));
    assert(near(parsed.ac.frequencyHz, 50) && parsed.ac.counterWh == 1000);
    sms[strlen(sms) - 1] = sms[strlen(sms) - 1] == 'A' ? 'B' : 'A';
    assert(decodeD3(sms, publicTestKey, true, hostHmac, parsed) == D2Error::AuthInvalid);
    frame.part = 1; frame.sequence = 2;
    assert(encodeD3(frame, publicTestKey, true, hostHmac, sms) == D2Error::Ok);
    assert(decodeD3(sms, publicTestKey, true, hostHmac, parsed) == D2Error::Ok);
    assert(parsed.dc[0].valid && !parsed.dc[2].valid && parsed.dc[2].fault == MeasureFault::Io);
    frame.dc[2].power = 10;
    assert(encodeD3(frame, publicTestKey, true, hostHmac, sms) == D2Error::NonCanonicalAbsent && !*sms);
    frame = vectorFrame(); frame.ac.electric.energyComplete = true; frame.ac.energyFault = MeasureFault::CounterReset;
    assert(encodeD3(frame, publicTestKey, true, hostHmac, sms) == D2Error::FlagDependency);
    frame = vectorFrame(); frame.battery.current = 31;
    assert(encodeD3(frame, publicTestKey, true, hostHmac, sms) == D2Error::OutOfRange);
    frame = vectorFrame(); assert(encodeD3(frame, nullptr, true, hostHmac, sms) == D2Error::AuthRequired);
    TestStore store; D2Journal journal(store, hostHmac); journal.begin();
    assert(journal.provision("TEST-001", publicTestKey, "+243810000001", "PUBLIC-TEST"));
    D2Frame old; old.interval = 1800; D2Error error;
    assert(journal.enqueue(old, error) == EnqueueResult::Stored);
    const std::string oldText = journal.at(0)->text;
    frame = vectorFrame(); assert(journal.enqueueV3(frame, error) == EnqueueResult::Stored && journal.count() == 3);
    const std::string first = journal.at(1)->text, second = journal.at(2)->text;
    assert(journal.at(1)->sequence + 1 == journal.at(2)->sequence);
    D2Journal reboot(store, hostHmac); reboot.begin();
    assert(reboot.state() == D2State::Ready && reboot.count() == 3);
    assert(oldText == reboot.at(0)->text && first == reboot.at(1)->text && second == reboot.at(2)->text);
    assert(reboot.recordAttempt(1) && first == reboot.at(1)->text);
    assert(reboot.acceptedByModem(1));
    D2Journal partial(store, hostHmac); partial.begin();
    assert(partial.state() == D2State::Ready && partial.count() == 2 && second == partial.at(1)->text);
    TestStore failing; D2Journal atomic(failing, hostHmac); atomic.begin();
    assert(atomic.provision("TEST-001", publicTestKey, "+243810000001", "PUBLIC-TEST"));
    failing.failAt = failing.writes + 2;
    assert(atomic.enqueueV3(frame, error) == EnqueueResult::StorageError && atomic.count() == 0);
    D2Journal recovered(failing, hostHmac); recovered.begin();
    assert(recovered.state() == D2State::Ready && recovered.count() == 0);
    assert(recovered.enqueueV3(frame, error) == EnqueueResult::Stored);
    assert(recovered.at(0)->sequence == 257);
    assert(recovered.enqueueV3(frame, error) == EnqueueResult::Stored);
    assert(recovered.enqueueV3(frame, error) == EnqueueResult::Full);
    puts("PASS D3: length/HMAC, fields, absence, ranges, paired atomic NVS, legacy migration, reboot, immutable retries, capacity");
}
int main(int argc, char** argv) {
    for (unsigned i = 0; i < 32; ++i) publicTestKey[i] = static_cast<uint8_t>(i + 1);
    if (argc == 2 && !strcmp(argv[1], "vector")) {
        auto frame = vectorFrame(); char sms[D2_SMS_CAPACITY];
        for (uint8_t part = 0; part < 2; ++part) {
            frame.part = part; frame.sequence = frame.snapshot + part;
            assert(encodeD3(frame, publicTestKey, true, hostHmac, sms) == D2Error::Ok); puts(sms);
        }
        return 0;
    }
    codecTests();
    modemAndLocationTests();
    pzemTests();
    Registers port; I2cBus bus(port);
    Ina228 battery(bus, {0x40, {.0025, 30, 32, 3, 1}});
    port.ina228(0x40);
    assert(battery.begin() && battery.calibration() == 1875);
    assert(battery.start(0)); auto value = battery.read(200);
    assert(value.valid && near(value.voltage, 12) && near(value.current, 10));
    assert(near(value.power, 120, .02));
    port.reg(0x40, 4) = (1048576U - 80000U) << 4;
    port.reg(0x40, 7) = (1048576U - 174763U) << 4;
    assert(battery.start(1000)); value = battery.read(1200);
    assert(value.valid && value.current < 0 && near(value.power, -120, .02));
    Ina228 duplicate(bus, {0x40, {.0025, 30, 32, 3, 1}});
    assert(!duplicate.begin() && duplicate.fault() == MeasureFault::AddressConflict);
    Ina228 impossible(bus, {0x41, {.0025, 30, 32, 3, 1}, true});
    assert(!impossible.begin()); // 75 mV cannot fit +/-40.96 mV
    Ina228 overheating(bus, {0x41, {.0025, 30, 32, 2, 1}});
    assert(!overheating.begin());
    Ina228 narrow(bus, {0x41, {.0025, 10, 32, 3, -1}, true});
    port.ina228(0x41); assert(narrow.begin() && narrow.calibration() == 2500);
    port.reg(0x41, 4) = 128000U << 4; // 10 mV, narrow range, 4 A
    port.reg(0x41, 7) = 209715U << 4;
    assert(narrow.start(0)); value = narrow.read(200);
    assert(value.valid && near(value.current, -4));
    assert(battery.start(2000)); port.reg(0x40, 0x0b) = 1;
    assert(battery.read(2200).fault == MeasureFault::NotReady);
    assert(battery.read(3000).fault == MeasureFault::Timeout);
    assert(battery.start(4000)); port.reg(0x40, 0x0b) = 0x203;
    assert(battery.read(4200).fault == MeasureFault::Overflow);
    assert(battery.start(5000)); port.reg(0x40, 0x0b) = 3;
    port.reg(0x40, 2) = 0;
    assert(!battery.read(5200).valid); // reset/calibration lost

    const ShuntConfig configs[3] = {{.01, 5, 26, 1, 1}, {.01, 5, 26, 1, -1}, {.01, 5, 26, 1, 1}};
    Ina3221 loads(bus, 0x42, configs);
    port.reg(0x42, 0xfe) = 0x5449; port.reg(0x42, 0xff) = 0x3220;
    port.reg(0x42, 0xf) = 1;
    for (uint8_t channel = 0; channel < 3; ++channel) {
        port.reg(0x42, static_cast<uint8_t>(1 + 2 * channel)) = 250U << 3; // 10 mV -> 1 A
        port.reg(0x42, static_cast<uint8_t>(2 + 2 * channel)) = 1500U << 3;
    }
    assert(loads.begin() && loads.start(0)); ElectricalReading readings[3];
    assert(loads.read(20, readings));
    assert(readings[0].valid && near(readings[0].power, 12));
    assert(readings[1].valid && near(readings[1].power, -12));
    assert(readings[2].valid);
    port.reg(0x42, 5) = 2000U << 3; // 8 A, third port only exceeds configured limit
    assert(loads.start(100)); assert(loads.read(120, readings));
    assert(readings[0].valid && readings[1].valid && !readings[2].valid);
    port.failReg = 3;
    assert(loads.start(200)); assert(loads.read(220, readings));
    assert(readings[0].valid && readings[1].fault == MeasureFault::Io);
    port.failReg = -1;
    EnergyWindow energy;
    energy.sample(UINT32_MAX - 4999, -360, true); energy.sample(5000, -360, true);
    assert(energy.complete(5000) && near(energy.wattHours(), -1));
    energy.sample(15000, 0, false); assert(!energy.complete(15000));
    puts("PASS INA228/INA3221: calibration, polarity, three channels, conflicts, ranges, reset, faults, energy");
}
