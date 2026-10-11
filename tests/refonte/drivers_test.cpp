#include "../../src/drivers/ina228.h"
#include "../../src/drivers/ina3221.h"
#include "../../src/drivers/pzem004t.h"
#include "../../src/communication/gsm.h"
#include "../../src/location/location.h"
#include "../../src/telemetry/energy_window.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <utility>
#include <deque>
#include <vector>
#include <string>
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
int main() {
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
