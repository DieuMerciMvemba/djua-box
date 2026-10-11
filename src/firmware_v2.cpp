#include "config.h"
#include "firmware_v2.h"
#include "pins.h"
#include <Arduino.h>
#include <Wire.h>
#include <SoftwareSerial.h>
#include "drivers/ina228.h"
#include "drivers/ina3221.h"
#include "drivers/pzem004t.h"
#include "telemetry/energy_window.h"
#include "telemetry/telemetry.h"
#include "communication/sms.h"
#include "communication/mqtt.h"
#include "communication/internet.h"
#include "sensors/gps.h"
#include "sensors/rtc_ds1302.h"
#include "geofencing/geofence.h"
#include "indicators/indicators.h"
#include "status/device_status.h"

namespace {
using namespace djua;
class WirePort : public I2cPort {
public:
    bool transfer(uint8_t address, const uint8_t* tx, size_t size, uint8_t* rx, size_t count) override {
        Wire.beginTransmission(address);
        if (Wire.write(tx, size) != size || Wire.endTransmission(count == 0) != 0) return false;
        if (!count) return true;
        if (Wire.requestFrom(address, static_cast<uint8_t>(count)) != count) return false;
        for (size_t i = 0; i < count; ++i) { int c = Wire.read(); if (c < 0) return false; rx[i] = static_cast<uint8_t>(c); }
        return true;
    }
};
WirePort wirePort; I2cBus bus(wirePort);
Ina228 battery(bus, {V2_BATTERY_ADDRESS, {BATTERY_SHUNT_OHMS, BATTERY_MAX_CURRENT_A,
    BATTERY_MAX_VALID_V, BATTERY_SHUNT_RATED_W, BATTERY_CURRENT_POLARITY}, BATTERY_ADC_NARROW_RANGE != 0});
Ina228 solar(bus, {V2_SOLAR_ADDRESS, {SOLAR_SHUNT_OHMS, SOLAR_MAX_CURRENT_A,
    SOLAR_MAX_BUS_V, SOLAR_SHUNT_RATED_W, SOLAR_CURRENT_POLARITY}, SOLAR_ADC_NARROW_RANGE != 0});
Ina228 tv(bus, {V2_TV_ADDRESS, {TV_SHUNT_OHMS, TV_MAX_CURRENT_A,
    TV_MAX_BUS_V, TV_SHUNT_RATED_W, TV_CURRENT_POLARITY}, TV_ADC_NARROW_RANGE != 0});
const ShuntConfig dcConfig[3] = {
    {DC1_SHUNT_OHMS, DC1_MAX_CURRENT_A, 26, DC1_SHUNT_RATED_W, DC1_CURRENT_POLARITY},
    {DC2_SHUNT_OHMS, DC2_MAX_CURRENT_A, 26, DC2_SHUNT_RATED_W, DC2_CURRENT_POLARITY},
    {DC3_SHUNT_OHMS, DC3_MAX_CURRENT_A, 26, DC3_SHUNT_RATED_W, DC3_CURRENT_POLARITY}
};
Ina3221 dc(bus, V2_DC_ADDRESS, dcConfig);
class PzemSerial : public ByteStream {
public:
    EspSoftwareSerial::UART uart;
    bool enabled = false;
    int available() override { return enabled ? uart.available() : 0; }
    int read() override { return enabled ? uart.read() : -1; }
    bool write(const uint8_t* p, size_t n) override { return enabled && uart.write(p, n) == n; }
};
PzemSerial pzemIo;
Pzem004t pzem(pzemIo, PZEM_MODBUS_ADDRESS, PZEM_REPLY_TIMEOUT_MS);
class GnssProvider : public LocationProvider {
    void update() override { updateGPS(); }
    LocationFix read() override { return readGPS(); }
};
GnssProvider gnss;
LocationService location(gnss); // Optional cellular provider can be injected here; no modem GNSS.
ElectricalReading readings[6]; // BAT, SOL, DC1, DC2, DC3, TV
EnergyWindow windows[6];
CounterWindow acWindow;
bool done[6] = {}, acquiring = false;
uint32_t lastAcquisition = 0, lastPoll = 0, lastGeofence = 0, windowStart = 0;
bool windowStarted = false;

constexpr bool reservedPin(int pin) {
    return pin == I2C_SDA_PIN || pin == I2C_SCL_PIN || pin == GPS_RX_PIN || pin == GPS_TX_PIN ||
        pin == GSM_RX_PIN || pin == GSM_TX_PIN || pin == GSM_PWRKEY_PIN || pin == DS1302_DATA_PIN ||
        pin == DS1302_CLOCK_PIN || pin == DS1302_CE_PIN || pin == LED_SYSTEM_PIN ||
        pin == LED_COMM_PIN || pin == LED_ALERT_PIN || pin == BUZZER_PIN;
}
static_assert(PZEM_RX_PIN != PZEM_TX_PIN && !reservedPin(PZEM_RX_PIN) && !reservedPin(PZEM_TX_PIN),
              "Conflit PZEM UART logiciel / GPS / modem / PWRKEY / autres broches");
static_assert(PZEM_TX_PIN >= 0 && PZEM_TX_PIN <= 33 && PZEM_RX_PIN >= 0 && PZEM_RX_PIN <= 39,
              "PZEM TX doit etre une sortie ; RX une entree valide");
void addClock(TelemetryV3& frame, const RTCData& clock) {
    frame.rtcValid = clock.valid && clock.running && d2UtcEpoch(clock.year, clock.month,
        clock.day, clock.hour, clock.minute, clock.second, RTC_GMT_OFFSET_MINUTES, frame.rtc);
}
void beginAcquisition(uint32_t now) {
    acquiring = true;
    for (unsigned i = 0; i < 6; ++i) { readings[i] = {}; readings[i].sampledAt = now; done[i] = true; }
    if (V2_BATTERY_CONFIGURED) { done[0] = !battery.start(now); readings[0].fault = battery.fault(); }
    if (V2_SOLAR_CONFIGURED) { done[1] = !solar.start(now); readings[1].fault = solar.fault(); }
    if (V2_TV_CONFIGURED) { done[5] = !tv.start(now); readings[5].fault = tv.fault(); }
    if (V2_DC_CONFIGURED) {
        const bool started = dc.start(now);
        for (unsigned i = 2; i < 5; ++i) { done[i] = !started; readings[i].fault = dc.fault(); }
    }
}
bool collectAcquisition(uint32_t now) {
    const unsigned indices[] = {0, 1, 5}; Ina228* sensors[] = {&battery, &solar, &tv};
    for (unsigned i = 0; i < 3; ++i) if (!done[indices[i]]) {
        auto r = sensors[i]->read(now);
        if (r.fault != MeasureFault::NotReady) { readings[indices[i]] = r; done[indices[i]] = true; }
    }
    if (!done[2]) {
        ElectricalReading ports[3];
        if (dc.read(now, ports)) for (unsigned i = 0; i < 3; ++i) { readings[2 + i] = ports[i]; done[2 + i] = true; }
    }
    for (bool complete : done) if (!complete) return false;
    // Project battery limits are separate from sensor bus limits.
    if (readings[0].valid && readings[0].voltage < BATTERY_MIN_VALID_V) {
        readings[0].valid = false; readings[0].fault = MeasureFault::Range;
    }
    return true;
}
void sampleWindow(uint32_t now) {
    const AcReading ac = pzem.reading(now, PZEM_MAX_AGE_MS);
    for (unsigned i = 0; i < 6; ++i)
        windows[i].sample(now, readings[i].power, readings[i].valid, D2_ENERGY_MAX_GAP_MS);
    acWindow.sample(now, ac.counterWh, ac.counterValid, D2_ENERGY_MAX_GAP_MS);
    if (!windowStarted) { windowStarted = true; windowStart = now; }
    static const char* names[] = {"BAT", "SOL", "DC1", "DC2", "DC3", "TV"};
    bool valid[6], available[6];
    for (unsigned i = 0; i < 6; ++i) {
        const auto& r = readings[i]; valid[i] = r.valid;
        available[i] = r.fault != MeasureFault::Io && r.fault != MeasureFault::WrongDevice;
        Serial.printf("[V2 %s] valid=%u fault=%u V=%.3f I=%.3f P=%.2f energy_complete=%u\n",
            names[i], r.valid, static_cast<unsigned>(r.fault), r.voltage, r.current, r.power, windows[i].complete(now));
    }
    Serial.printf("[V2 AC] valid=%u fault=%u V=%.1f I=%.3f P=%.1f S=%.1f PF=%.2f f=%.1f counter=%lu Wh energy_complete=%u\n",
        ac.electric.valid, static_cast<unsigned>(ac.electric.fault), ac.electric.voltage,
        ac.electric.current, ac.electric.power, ac.apparentVA, ac.powerFactor, ac.frequencyHz,
        static_cast<unsigned long>(ac.counterWh), acWindow.complete(now));
    const RTCData clock = readRTC();
    recordV2SensorStatus(available, valid, ac.electric.valid, clock.valid, static_cast<float>(readings[0].voltage));
#if DJUA_COMM_PROFILE == DJUA_PROFILE_SMS_PRODUCTION
    if (now - windowStart < D2_PERIOD_SECONDS * 1000UL) return;
#else
    if (now == windowStart) return;
#endif
    TelemetryV3 frame;
    frame.uptime = now; frame.interval = (now - windowStart + 500U) / 1000U; addClock(frame, clock);
    frame.location = location.read(GPS_MAX_AGE_MS);
    if (!frame.location.valid) { frame.location.latitude = frame.location.longitude = 0; frame.location.accuracyKnown = false; }
    if (!frame.location.accuracyKnown) frame.location.accuracyM = 0;
    frame.battery = makeSlot(readings[0], now, windows[0].wattHours(), windows[0].complete(now));
    frame.solar = makeSlot(readings[1], now, windows[1].wattHours(), windows[1].complete(now));
    for (unsigned i = 0; i < 4; ++i) frame.dc[i] = makeSlot(readings[2 + i], now, windows[2 + i].wattHours(), windows[2 + i].complete(now));
    frame.ac.electric = makeSlot(ac.electric, now, acWindow.wattHours(), acWindow.complete(now));
    frame.ac.energyFault = acWindow.fault();
    if (ac.electric.valid) {
        frame.ac.apparentVA = ac.apparentVA; frame.ac.frequencyHz = ac.frequencyHz;
        frame.ac.powerFactor = ac.powerFactor; frame.ac.alarm = ac.alarm;
        frame.ac.counterValid = ac.counterValid; if (ac.counterValid) frame.ac.counterWh = ac.counterWh;
    }
#if DJUA_COMM_PROFILE == DJUA_PROFILE_SMS_PRODUCTION
    if (D3_RECEIVER_ENABLED) enqueueSMSV3(frame);
    else Serial.println("[D3] Mesures locales disponibles ; emission D3 desactivee jusqu'a adaptation du recepteur");
#else
    const TelemetryData data = buildV3Telemetry(frame, clock);
#if DJUA_COMM_PROFILE == DJUA_PROFILE_MQTT_TEST
    const bool accepted = sendTelemetryToMQTT(data);
    recordTelemetryStatus(accepted, false);
#else
    const bool accepted = sendTelemetryToBackend(data);
    recordTelemetryStatus(false, accepted);
#endif
#endif
    for (unsigned i = 0; i < 6; ++i) windows[i].reset(now, readings[i].power, readings[i].valid);
    acWindow.reset(now, ac.counterWh, ac.counterValid); windowStart = now;
}
void checkGeofence(uint32_t now) {
    if (!GEOFENCE_ENABLED || now - lastGeofence < GEOFENCE_CHECK_INTERVAL_MS) return;
    lastGeofence = now;
    const auto fix = location.read(GPS_MAX_AGE_MS); const auto result = updateGeofence(fix);
    recordGeofenceStatus(result.exitEvent, result.enterEvent);
#if DJUA_COMM_PROFILE == DJUA_PROFILE_MQTT_TEST
    const RTCData clock = readRTC(); char stamp[32] = {};
    const bool valid = formatRTCTimestamp(clock, stamp, sizeof(stamp)); sendGeofenceToMQTT(result, stamp, valid);
#endif
#if DJUA_COMM_PROFILE == DJUA_PROFILE_SMS_PRODUCTION
    if (result.exitEvent || result.enterEvent) {
        D2Frame event; event.event = true; event.exit = result.exitEvent; event.uptime = now;
        const RTCData clock = readRTC();
        if (clock.valid && clock.running && d2UtcEpoch(clock.year, clock.month, clock.day, clock.hour,
            clock.minute, clock.second, RTC_GMT_OFFSET_MINUTES, event.rtc)) event.flags |= 1;
        event.flags |= 2; event.values[Lat] = {fix.latitude, true}; event.values[Lon] = {fix.longitude, true};
        if (result.distanceMeters >= 0 && result.distanceMeters <= 1000000) {
            event.flags |= 4; event.values[Distance] = {result.distanceMeters, true};
        }
        enqueueSMS(event);
    }
#endif
}
}
void setupV2() {
    Serial.begin(SERIAL_BAUD_RATE); initIndicators(); setSystemIndicatorState(SYSTEM_BOOTING);
    Serial.println("[V2] INA3221 + 3 INA228 / PZEM UART logiciel / NEO-8M / A7670");
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN); Wire.setClock(V2_I2C_CLOCK_HZ); Wire.setTimeOut(50);
    if (V2_BATTERY_CONFIGURED) battery.begin();
    if (V2_SOLAR_CONFIGURED) solar.begin();
    if (V2_TV_CONFIGURED) tv.begin();
    if (V2_DC_CONFIGURED) dc.begin();
    if (PZEM_HARDWARE_CONFIGURED) {
        pzemIo.uart.begin(PZEM_BAUD_RATE, SWSERIAL_8N1, PZEM_RX_PIN, PZEM_TX_PIN, false, 128, 1280);
        pzemIo.enabled = static_cast<bool>(pzemIo.uart);
        Serial.println(pzemIo.enabled ? "[PZEM] UART logiciel configure (precision non validee)" : "[PZEM] Broches/configuration invalides");
    }
    initGPS(); initGeofence(); initRTC(); initDeviceStatus(true, isRTCAvailable());
#if DJUA_COMM_PROFILE == DJUA_PROFILE_SMS_PRODUCTION
    initSMS();
#else
    initInternet();
#if DJUA_COMM_PROFILE == DJUA_PROFILE_MQTT_TEST
    initMQTT();
#endif
#endif
    lastAcquisition = millis() - ELECTRICAL_SAMPLE_INTERVAL_MS;
    lastPoll = millis() - PZEM_POLL_INTERVAL_MS; lastGeofence = millis();
    setSystemIndicatorState(SYSTEM_RUNNING); updateIndicators();
}
void loopV2() {
    const uint32_t now = millis(); updateIndicators(); location.update(); checkGeofence(now);
    pzem.update(now);
    if (pzemIo.enabled && !pzem.busy() && now - lastPoll >= PZEM_POLL_INTERVAL_MS) {
        lastPoll = now; pzem.start(now);
    }
#if DJUA_COMM_PROFILE == DJUA_PROFILE_SMS_PRODUCTION
    updateSMS(); updateSMSDeviceStatus(isSMSNetworkRegistered(), isSMSConnecting(), isSMSHealthy(), hasPendingSMS(), isSMSFaulted());
#else
    updateInternet();
#if DJUA_COMM_PROFILE == DJUA_PROFILE_MQTT_TEST
    updateMQTT();
#endif
    updateDeviceStatus(isInternetConnected(), isInternetConnecting(), isMQTTConnected());
#endif
    const uint32_t acquisitionNow = millis();
    if (!acquiring && acquisitionNow - lastAcquisition >= ELECTRICAL_SAMPLE_INTERVAL_MS) {
        lastAcquisition = acquisitionNow; beginAcquisition(acquisitionNow);
    }
    if (acquiring && collectAcquisition(acquisitionNow)) { acquiring = false; sampleWindow(millis()); }
    updateIndicators();
}
