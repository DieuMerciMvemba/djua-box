// =====================================================================
// IOT BOX V2 - FIRMWARE PRINCIPAL ESP32
// =====================================================================
// Architecture materielle V2 :
//   - Bus I2C (GPIO 21/22) : 3x INA228 (Batterie 0x44, Solaire 0x41, TV 0x45)
//                            + 1x INA3221 (Charges DC1, DC2, DC3 sur 0x40)
//   - Mesure AC            : PZEM-004T (UART logiciel RX 35 / TX 14, Modbus-RTU)
//   - Positionnement GNSS  : NEO-8M (UART2 RX 16 / TX 17)
//   - Horloge temps reel   : DS1302 (Three-Wire DATA 26 / CLK 25 / CE 27)
//   - Modem cellulaire GSM : A7670 (UART1 RX 32 / TX 13, 115200 bauds)
//   - Indicateurs locaux   : LEDs SYSTEM (23), COMM (19), ALERT (18), Buzzer (33)
// =====================================================================

#include <Arduino.h>
#include <Wire.h>
#include <SoftwareSerial.h>

// 1. Configuration et brochage
#include "src/config.h"
#include "src/pins.h"

// 2. Pilotes electriques V2 (I2C et Modbus)
#include "src/drivers/ina228.h"
#include "src/drivers/ina3221.h"
#include "src/drivers/pzem004t.h"

// 3. Capteurs environnementaux et horloge
#include "src/sensors/gps.h"
#include "src/sensors/rtc_ds1302.h"
#include "src/geofencing/geofence.h"

// 4. Integration energetique et modeles de telemetrie
#include "src/telemetry/energy_window.h"
#include "src/telemetry/telemetry.h"

// 5. Transports de communication (SMS D2T2/D2E, Internet, MQTT)
#include "src/communication/sms.h"
#include "src/communication/internet.h"
#include "src/communication/mqtt.h"

// 6. Supervision locale et signalisation (LEDs + Buzzer)
#include "src/indicators/indicators.h"
#include "src/status/device_status.h"

// =====================================================================
// ADAPTATEURS MATERIELS & INSTANCES GLOBALES
// =====================================================================
namespace {
using namespace djua;

/** Adaptateur Arduino TwoWire (Wire) pour le bus I2C abstrait des capteurs INA. */
class WirePort : public I2cPort {
public:
    bool transfer(uint8_t address, const uint8_t* tx, size_t size, uint8_t* rx, size_t count) override {
        Wire.beginTransmission(address);
        if (Wire.write(tx, size) != size || Wire.endTransmission(count == 0) != 0) return false;
        if (!count) return true;
        if (Wire.requestFrom(address, static_cast<uint8_t>(count)) != count) return false;
        for (size_t i = 0; i < count; ++i) {
            int c = Wire.read();
            if (c < 0) return false;
            rx[i] = static_cast<uint8_t>(c);
        }
        return true;
    }
};

/** Adaptateur EspSoftwareSerial pour le compteur AC PZEM-004T. */
class PzemSerial : public ByteStream {
public:
    EspSoftwareSerial::UART uart;
    bool enabled = false;
    int available() override { return enabled ? uart.available() : 0; }
    int read() override { return enabled ? uart.read() : -1; }
    bool write(const uint8_t* p, size_t n) override { return enabled && uart.write(p, n) == n; }
};

/** Adaptateur GNSS reliant le pilote NEO-8M au service de localisation. */
class GnssProvider : public LocationProvider {
    void update() override { updateGPS(); }
    LocationFix read() override { return readGPS(); }
};

// Bus I2C et capteurs de puissance DC (INA228 + INA3221)
WirePort wirePort;
I2cBus bus(wirePort);

Ina228 battery(bus, {
    V2_BATTERY_ADDRESS,
    {BATTERY_SHUNT_OHMS, BATTERY_MAX_CURRENT_A, BATTERY_MAX_VALID_V, BATTERY_SHUNT_RATED_W, BATTERY_CURRENT_POLARITY},
    BATTERY_ADC_NARROW_RANGE != 0
});

Ina228 solar(bus, {
    V2_SOLAR_ADDRESS,
    {SOLAR_SHUNT_OHMS, SOLAR_MAX_CURRENT_A, SOLAR_MAX_BUS_V, SOLAR_SHUNT_RATED_W, SOLAR_CURRENT_POLARITY},
    SOLAR_ADC_NARROW_RANGE != 0
});

Ina228 tv(bus, {
    V2_TV_ADDRESS,
    {TV_SHUNT_OHMS, TV_MAX_CURRENT_A, TV_MAX_BUS_V, TV_SHUNT_RATED_W, TV_CURRENT_POLARITY},
    TV_ADC_NARROW_RANGE != 0
});

const ShuntConfig dcConfig[3] = {
    {DC1_SHUNT_OHMS, DC1_MAX_CURRENT_A, 26, DC1_SHUNT_RATED_W, DC1_CURRENT_POLARITY},
    {DC2_SHUNT_OHMS, DC2_MAX_CURRENT_A, 26, DC2_SHUNT_RATED_W, DC2_CURRENT_POLARITY},
    {DC3_SHUNT_OHMS, DC3_MAX_CURRENT_A, 26, DC3_SHUNT_RATED_W, DC3_CURRENT_POLARITY}
};
Ina3221 dc(bus, V2_DC_ADDRESS, dcConfig);

// Capteur AC PZEM-004T et localisation GNSS
PzemSerial pzemIo;
Pzem004t pzem(pzemIo, PZEM_MODBUS_ADDRESS, PZEM_REPLY_TIMEOUT_MS);
GnssProvider gnss;
LocationService location(gnss);

// Etats d'acquisition et fenetres d'integration energetique (0:BAT, 1:SOL, 2:DC1, 3:DC2, 4:DC3, 5:TV)
ElectricalReading readings[6];
EnergyWindow windows[6];
CounterWindow acWindow;
bool done[6] = {};
bool acquiring = false;
bool windowStarted = false;
uint32_t lastAcquisition = 0;
uint32_t lastPoll = 0;
uint32_t lastGeofence = 0;
uint32_t windowStart = 0;

// Verification a la compilation de l'absence de conflit de broches avec le PZEM
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
} // namespace

// =====================================================================
// ACQUISITION ELECTRIQUE NON BLOQUANTE & TELEMETRIE
// =====================================================================

/** Convertit l'horloge locale DS1302 (GMT+1) en epoch UTC pour la trame de telemetrie. */
static void addClock(djua::TelemetryV3& frame, const RTCData& clock) {
    frame.rtcValid = clock.valid && clock.running && djua::d2UtcEpoch(
        clock.year, clock.month, clock.day, clock.hour, clock.minute, clock.second,
        RTC_GMT_OFFSET_MINUTES, frame.rtc);
}

/** Declenche en parallele une conversion single-shot sur tous les capteurs INA configures. */
static void beginAcquisition(uint32_t now) {
    acquiring = true;
    for (unsigned i = 0; i < 6; ++i) {
        readings[i] = {};
        readings[i].sampledAt = now;
        done[i] = true;
    }
    if (V2_BATTERY_CONFIGURED) { done[0] = !battery.start(now); readings[0].fault = battery.fault(); }
    if (V2_SOLAR_CONFIGURED)   { done[1] = !solar.start(now);   readings[1].fault = solar.fault(); }
    if (V2_TV_CONFIGURED)      { done[5] = !tv.start(now);      readings[5].fault = tv.fault(); }
    if (V2_DC_CONFIGURED) {
        const bool started = dc.start(now);
        for (unsigned i = 2; i < 5; ++i) {
            done[i] = !started;
            readings[i].fault = dc.fault();
        }
    }
}

/** Collecte les mesures INA des qu'elles sont pretes (ou en defaut/timeout sans bloquer). */
static bool collectAcquisition(uint32_t now) {
    const unsigned indices[] = {0, 1, 5};
    djua::Ina228* sensors[] = {&battery, &solar, &tv};
    for (unsigned i = 0; i < 3; ++i) {
        if (!done[indices[i]]) {
            auto r = sensors[i]->read(now);
            if (r.fault != djua::MeasureFault::NotReady) {
                readings[indices[i]] = r;
                done[indices[i]] = true;
            }
        }
    }
    if (!done[2]) {
        djua::ElectricalReading ports[3];
        if (dc.read(now, ports)) {
            for (unsigned i = 0; i < 3; ++i) {
                readings[2 + i] = ports[i];
                done[2 + i] = true;
            }
        }
    }
    for (bool complete : done) {
        if (!complete) return false;
    }
    // Controle du seuil minimal de tension batterie (9.0 V)
    if (readings[0].valid && readings[0].voltage < BATTERY_MIN_VALID_V) {
        readings[0].valid = false;
        readings[0].fault = djua::MeasureFault::Range;
    }
    return true;
}

/** Integre les energies de la fenetre et met en file le SMS D2T2 lorsque la periode est ecoulee. */
static void sampleWindow(uint32_t now) {
    const djua::AcReading ac = pzem.reading(now, PZEM_MAX_AGE_MS);
    for (unsigned i = 0; i < 6; ++i) {
        windows[i].sample(now, readings[i].power, readings[i].valid, D2_ENERGY_MAX_GAP_MS);
    }
    acWindow.sample(now, ac.counterWh, ac.counterValid, D2_ENERGY_MAX_GAP_MS);
    if (!windowStarted) {
        windowStarted = true;
        windowStart = now;
    }

    static const char* names[] = {"BAT", "SOL", "DC1", "DC2", "DC3", "TV"};
    bool valid[6], available[6];
    for (unsigned i = 0; i < 6; ++i) {
        const auto& r = readings[i];
        valid[i] = r.valid;
        available[i] = r.fault != djua::MeasureFault::Io && r.fault != djua::MeasureFault::WrongDevice;
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

    djua::TelemetryV3 frame;
    frame.uptime = now;
    frame.interval = (now - windowStart + 500U) / 1000U;
    addClock(frame, clock);
    frame.location = location.read(GPS_MAX_AGE_MS);
    if (!frame.location.valid) {
        frame.location.latitude = frame.location.longitude = 0;
        frame.location.accuracyKnown = false;
    }
    if (!frame.location.accuracyKnown) frame.location.accuracyM = 0;

    frame.battery = djua::makeSlot(readings[0], now, windows[0].wattHours(), windows[0].complete(now));
    frame.solar   = djua::makeSlot(readings[1], now, windows[1].wattHours(), windows[1].complete(now));
    for (unsigned i = 0; i < 4; ++i) {
        frame.dc[i] = djua::makeSlot(readings[2 + i], now, windows[2 + i].wattHours(), windows[2 + i].complete(now));
    }
    frame.ac.electric = djua::makeSlot(ac.electric, now, acWindow.wattHours(), acWindow.complete(now));
    frame.ac.energyFault = acWindow.fault();
    if (ac.electric.valid) {
        frame.ac.apparentVA   = ac.apparentVA;
        frame.ac.frequencyHz  = ac.frequencyHz;
        frame.ac.powerFactor  = ac.powerFactor;
        frame.ac.alarm        = ac.alarm;
        frame.ac.counterValid = ac.counterValid;
        if (ac.counterValid) frame.ac.counterWh = ac.counterWh;
    }

#if DJUA_COMM_PROFILE == DJUA_PROFILE_SMS_PRODUCTION
    enqueueSMS(buildD2TelemetryFrame(frame));
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

    for (unsigned i = 0; i < 6; ++i) {
        windows[i].reset(now, readings[i].power, readings[i].valid);
    }
    acWindow.reset(now, ac.counterWh, ac.counterValid);
    windowStart = now;
}

// =====================================================================
// SURVEILLANCE GEOFENCE & ALERTES D2E
// =====================================================================

/** Verifie periodiquement la position GPS et declenche un SMS urgent D2E en cas de sortie/entree. */
static void checkGeofence(uint32_t now) {
    if (!GEOFENCE_ENABLED || now - lastGeofence < GEOFENCE_CHECK_INTERVAL_MS) return;
    lastGeofence = now;
    const auto fix = location.read(GPS_MAX_AGE_MS);
    const auto result = updateGeofence(fix);
    recordGeofenceStatus(result.exitEvent, result.enterEvent);

#if DJUA_COMM_PROFILE == DJUA_PROFILE_MQTT_TEST
    const RTCData clock = readRTC();
    char stamp[32] = {};
    const bool valid = formatRTCTimestamp(clock, stamp, sizeof(stamp));
    sendGeofenceToMQTT(result, stamp, valid);
#endif

#if DJUA_COMM_PROFILE == DJUA_PROFILE_SMS_PRODUCTION
    if (result.exitEvent || result.enterEvent) {
        djua::D2Frame event;
        event.event = true;
        event.exit = result.exitEvent;
        event.uptime = now;
        const RTCData clock = readRTC();
        if (clock.valid && clock.running && djua::d2UtcEpoch(
                clock.year, clock.month, clock.day, clock.hour, clock.minute, clock.second,
                RTC_GMT_OFFSET_MINUTES, event.rtc)) {
            event.flags |= 1;
        }
        event.flags |= 2;
        event.values[djua::Lat] = {fix.latitude, true};
        event.values[djua::Lon] = {fix.longitude, true};
        if (result.distanceMeters >= 0 && result.distanceMeters <= 1000000) {
            event.flags |= 4;
            event.values[djua::Distance] = {result.distanceMeters, true};
        }
        enqueueSMS(event);
    }
#endif
}

// =====================================================================
// SETUP : INITIALISATION DU BOITIER IOT V2
// =====================================================================

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    initIndicators();
    setSystemIndicatorState(SYSTEM_BOOTING);

    Serial.println();
    Serial.println("=================================================");
    Serial.println("             DJUA IOT BOX V2 - ESP32             ");
    Serial.println("=================================================");
    Serial.printf("KIT ID : %s\n", DEVICE_ID);
    Serial.println("[V2] INA3221 + 3 INA228 / PZEM UART logiciel / NEO-8M / " GSM_MODULE_LABEL);

    // 1. Bus I2C avec timeout de securite (50 ms) contre tout blocage materiel
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(V2_I2C_CLOCK_HZ);
    Wire.setTimeOut(50);

    // 2. Capteurs DC I2C (tolere l'absence ou la panne d'un capteur, reprise auto en loop)
    if (V2_BATTERY_CONFIGURED) battery.begin();
    if (V2_SOLAR_CONFIGURED)   solar.begin();
    if (V2_TV_CONFIGURED)      tv.begin();
    if (V2_DC_CONFIGURED)      dc.begin();

    // 3. Compteur AC PZEM-004T sur UART logiciel
    if (PZEM_HARDWARE_CONFIGURED) {
        pzemIo.uart.begin(PZEM_BAUD_RATE, SWSERIAL_8N1, PZEM_RX_PIN, PZEM_TX_PIN, false, 128, 1280);
        pzemIo.enabled = static_cast<bool>(pzemIo.uart);
        Serial.println(pzemIo.enabled
            ? "[PZEM] UART logiciel configure"
            : "[PZEM] Broches/configuration invalides");
    }

    // 4. Positionnement GPS, Geofencing et Horloge RTC DS1302
    initGPS();
    initGeofence();
    initRTC();
    initDeviceStatus(true, isRTCAvailable());

    // 5. Transport de communication (SMS A7670 en production, ou Wi-Fi/MQTT en test)
#if DJUA_COMM_PROFILE == DJUA_PROFILE_SMS_PRODUCTION
    initSMS();
#else
    initInternet();
#if DJUA_COMM_PROFILE == DJUA_PROFILE_MQTT_TEST
    initMQTT();
#endif
#endif

    // Declenchement immediat du premier cycle de mesure
    lastAcquisition = millis() - ELECTRICAL_SAMPLE_INTERVAL_MS;
    lastPoll = millis() - PZEM_POLL_INTERVAL_MS;
    lastGeofence = millis();

    setSystemIndicatorState(SYSTEM_RUNNING);
    updateIndicators();
}

// =====================================================================
// LOOP : BOUCLE PRINCIPALE TEMPS REEL NON BLOQUANTE
// =====================================================================

void loop() {
    const uint32_t now = millis();

    // 1. Signalisation locale, lecture continue du flux GPS et verification Geofence
    updateIndicators();
    location.update();
    checkGeofence(now);

    // 2. Lecture asynchrone du compteur AC PZEM-004T
    pzem.update(now);
    if (pzemIo.enabled && !pzem.busy() && now - lastPoll >= PZEM_POLL_INTERVAL_MS) {
        lastPoll = now;
        pzem.start(now);
    }

    // 3. Ordonnanceur de communication (Modem GSM / File NVS ou Wi-Fi / MQTT)
#if DJUA_COMM_PROFILE == DJUA_PROFILE_SMS_PRODUCTION
    updateSMS();
    updateSMSDeviceStatus(
        isSMSNetworkRegistered(),
        isSMSConnecting(),
        isSMSHealthy(),
        hasPendingSMS(),
        isSMSFaulted()
    );
#else
    updateInternet();
#if DJUA_COMM_PROFILE == DJUA_PROFILE_MQTT_TEST
    updateMQTT();
#endif
    updateDeviceStatus(isInternetConnected(), isInternetConnecting(), isMQTTConnected());
#endif

    // 4. Echantillonnage electrique toutes les 10 s et emission periodique D2T2
    const uint32_t acquisitionNow = millis();
    if (!acquiring && acquisitionNow - lastAcquisition >= ELECTRICAL_SAMPLE_INTERVAL_MS) {
        lastAcquisition = acquisitionNow;
        beginAcquisition(acquisitionNow);
    }
    if (acquiring && collectAcquisition(acquisitionNow)) {
        acquiring = false;
        sampleWindow(millis());
    }

    updateIndicators();
}
