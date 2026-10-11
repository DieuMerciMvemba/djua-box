#include <Arduino.h>
#include "src/config.h"
#if IOT_HARDWARE_V2
#include "src/firmware_v2.h"
void setup() { setupV2(); }
void loop() { loopV2(); }
#else
#include "src/sensors/power_sensor.h"
#include "src/sensors/solar_power_sensor.h"
#include "src/sensors/dc_load_power_sensor.h"
#include "src/sensors/ac_power_sensor.h"
#include "src/sensors/gps.h"
#include "src/sensors/rtc_ds1302.h"
#include "src/geofencing/geofence.h"
#include "src/telemetry/telemetry.h"
#include "src/communication/internet.h"
#include "src/communication/mqtt.h"
#include "src/indicators/indicators.h"
#include "src/status/device_status.h"
#include "src/communication/sms.h"
#include "src/telemetry/energy_window.h"
static unsigned long lastTelemetryTime = 0;
static unsigned long lastGeofenceCheckTime = 0;
static djua::EnergyWindow dcWindow;

#if DJUA_COMM_PROFILE == DJUA_PROFILE_SMS_PRODUCTION
static djua::EnergyWindow solarWindow, acWindow;

static void addD2Clock(djua::D2Frame& frame, const RTCData& clock)
{
    if (clock.valid && clock.running && djua::d2UtcEpoch(clock.year, clock.month,
        clock.day, clock.hour, clock.minute, clock.second, RTC_GMT_OFFSET_MINUTES, frame.rtc))
        frame.flags |= 1;
}

static void updateCommunicationStatus()
{
    updateSMSDeviceStatus(isSMSNetworkRegistered(), isSMSConnecting(), isSMSHealthy(),
                          hasPendingSMS(), isSMSFaulted());
}

static void sampleD2Window(const PowerData& battery, const SolarPowerData& solar,
                           const ACPowerData& ac, const DcLoadPowerData& dc,
                           uint32_t batteryAt, uint32_t solarAt, uint32_t acAt,
                           uint32_t dcAt, const GPSData& gps, const RTCData& clock)
{
    const uint32_t now = millis();
    const bool batteryValid = battery.valid && now - batteryAt <= D2_ELECTRICAL_MAX_AGE_MS;
    const bool solarValid = solar.valid && now - solarAt <= D2_ELECTRICAL_MAX_AGE_MS;
    const bool acValid = ac.valid && now - acAt <= D2_ELECTRICAL_MAX_AGE_MS;
    const bool dcValid = dc.valid && now - dcAt <= D2_ELECTRICAL_MAX_AGE_MS;
    // Acquisition groupee : instant logique de fin de groupe pour les trapezes.
    solarWindow.sample(acAt, solar.power, solarValid, D2_ENERGY_MAX_GAP_MS);
    acWindow.sample(acAt, ac.activePower, acValid, D2_ENERGY_MAX_GAP_MS);
    dcWindow.sample(acAt, dc.power, dcValid, D2_ENERGY_MAX_GAP_MS);
    if (acWindow.durationMs(acAt) < D2_PERIOD_SECONDS * 1000UL) return;

    djua::D2Frame frame;
    frame.uptime = now; frame.interval = acWindow.durationSeconds(acAt);
    addD2Clock(frame, clock);
    if (gps.valid) {
        frame.flags |= 2; frame.values[djua::Lat] = {gps.latitude, true};
        frame.values[djua::Lon] = {gps.longitude, true};
    }
    if (batteryValid) {
        frame.flags |= 4; frame.values[djua::BatteryV] = {battery.voltage, true};
        frame.values[djua::BatteryI] = {battery.current, true}; frame.values[djua::BatteryP] = {battery.power, true};
    }
    if (solarValid) {
        frame.flags |= 8; frame.values[djua::SolarV] = {solar.voltage, true};
        frame.values[djua::SolarI] = {solar.current, true}; frame.values[djua::SolarP] = {solar.power, true};
        if (solarWindow.complete(now, D2_ELECTRICAL_MAX_AGE_MS)) {
            frame.flags |= 32; frame.values[djua::SolarE] = {solarWindow.wattHours(), true};
        }
    }
    if (acValid) {
        frame.flags |= 16; frame.values[djua::AcV] = {ac.voltageRms, true};
        frame.values[djua::AcI] = {ac.currentRms, true}; frame.values[djua::AcP] = {ac.activePower, true};
        frame.values[djua::AcS] = {ac.apparentPower, true};
        if (acWindow.complete(now, D2_ELECTRICAL_MAX_AGE_MS)) {
            frame.flags |= 64; frame.values[djua::AcE] = {acWindow.wattHours(), true};
        }
    }
    if (dcValid) {
        frame.flags |= 128; frame.values[djua::DcV] = {dc.voltage, true};
        frame.values[djua::DcI] = {dc.current, true}; frame.values[djua::DcP] = {dc.power, true};
        if (dcWindow.complete(now, D2_ELECTRICAL_MAX_AGE_MS)) {
            frame.flags |= 256; frame.values[djua::DcE] = {dcWindow.wattHours(), true};
        }
    }
    enqueueSMS(frame);
    // La frontiere appartient aux deux periodes, chaque trapeze a une seule.
    // La periode suivante progresse meme si la file est pleine ou le reseau absent.
    solarWindow.reset(acAt, solar.power, solarValid);
    acWindow.reset(acAt, ac.activePower, acValid);
    dcWindow.reset(acAt, dc.power, dcValid);
}
#else
static void updateCommunicationStatus()
{
    updateDeviceStatus(isInternetConnected(), isInternetConnecting(), isMQTTConnected());
}
#endif

static void reportGeofenceEvent(
    const GeofenceResult& result,
    const char* timestamp,
    bool timestampValid
)
{
    Serial.println(
        result.exitEvent
            ? "[ALERTE] GEOFENCE_EXIT"
            : "[INFO] GEOFENCE_ENTER"
    );

    Serial.print("[GEOFENCE_EVENT] device=");
    Serial.print(result.deviceId);
    Serial.print(" | type=");
    Serial.print(geofenceEventToString(result.eventType));
    Serial.print(" | timestamp=");
    Serial.print(timestampValid ? timestamp : "indisponible");
    Serial.print(" | latitude=");
    Serial.print(result.latitude, 6);
    Serial.print(" | longitude=");
    Serial.print(result.longitude, 6);
    Serial.print(" | distance_m=");
    Serial.println(result.distanceMeters, 1);

}

static void updateGeofenceMonitoring()
{
    if (!GEOFENCE_ENABLED)
    {
        return;
    }

    const unsigned long now = millis();

    if (
        now - lastGeofenceCheckTime <
        GEOFENCE_CHECK_INTERVAL_MS
    )
    {
        return;
    }

    // Ne pas rejouer plusieurs controles sur la meme position apres un retard.
    lastGeofenceCheckTime = now;

    const GPSData position = readGPS();
    const GeofenceResult result = updateGeofence(position);

    // Decision locale avant toute publication potentiellement synchrone.
    recordGeofenceStatus(result.exitEvent, result.enterEvent);
    updateIndicators();

    const RTCData clock = readRTC();
    char timestamp[32] = {};
    const bool timestampValid = formatRTCTimestamp(
        clock,
        timestamp,
        sizeof(timestamp)
    );

    // Les snapshots MQTT restent disponibles seulement dans le profil MQTT_TEST.
#if DJUA_COMM_PROFILE == DJUA_PROFILE_MQTT_TEST
    sendGeofenceToMQTT(result, timestamp, timestampValid);
#endif

    if (!result.positionUsable)
    {
        Serial.println(
            "[GEOFENCE] Verification ignoree : GPS invalide ou trop ancien"
        );
        return;
    }

    Serial.print("[GEOFENCE] GPS valide | Distance : ");
    Serial.print(result.distanceMeters, 1);
    Serial.print(" m | Etat : ");
    Serial.println(geofenceStateToString(result.state));

    if (result.confirmationCount > 0)
    {
        Serial.print("[GEOFENCE] Candidat ");
        Serial.print(geofenceStateToString(result.candidateState));
        Serial.print(' ');
        Serial.print(result.confirmationCount);
        Serial.print('/');
        Serial.println(GEOFENCE_CONFIRM_COUNT);
    }

    if (result.exitEvent || result.enterEvent)
    {
        reportGeofenceEvent(result, timestamp, timestampValid);
#if DJUA_COMM_PROFILE == DJUA_PROFILE_SMS_PRODUCTION
        djua::D2Frame event;
        event.event = true; event.exit = result.exitEvent; event.uptime = millis();
        addD2Clock(event, clock);
        if (result.positionUsable) {
            event.flags |= 2;
            event.values[djua::Lat] = {result.latitude, true};
            event.values[djua::Lon] = {result.longitude, true};
            if (result.distanceMeters >= 0 && result.distanceMeters <= 1000000) {
                event.flags |= 4; event.values[djua::Distance] = {result.distanceMeters, true};
            }
        }
        enqueueSMS(event);
#endif
    }
}
// =====================================================
// SETUP
// =====================================================

void setup()
{
    Serial.begin(SERIAL_BAUD_RATE);

    initIndicators();
    setSystemIndicatorState(SYSTEM_BOOTING);

    Serial.println();
    Serial.println("=================================");
    Serial.println("          IOT BOX V1");
    Serial.println("=================================");

    Serial.print("KIT ID : ");
    Serial.println(DEVICE_ID);

    // =================================================
    // INA219
    // =================================================

    Serial.println();
    Serial.println("[INA219] Initialisation...");

    if (initPowerSensor())
    {
        Serial.println("[INA219] Batterie OK");
    }
    else
    {
        Serial.println("[INA219] ERREUR");
    }

    // =================================================
    // INA219 SOLAIRE SUR LE MEME BUS I2C
    // =================================================

    Serial.println();
    updateIndicators();
#if SOLAR_MEASUREMENT_ENABLED
    Serial.println("[SOLAR INA219] Initialisation...");
    if (initSolarPowerSensor())
        Serial.printf("[SOLAR INA219] OK | adresse=0x%02X\n", SOLAR_INA219_I2C_ADDRESS);
    else
        Serial.println("[SOLAR INA219] INDISPONIBLE");
#else
    Serial.println("[SOLAR INA219] MESURE DESACTIVEE");
#endif

    // =================================================
    // INA219 DC : le bus I2C a deja ete initialise par la batterie.
    updateIndicators();
#if DC_LOAD_MEASUREMENT_ENABLED && DC_LOAD_SHUNT_CONFIGURED
    Serial.printf("[DC INA219] adresse=0x%02X | shunt=%.1f A / %.1f mV\n",
        DC_LOAD_INA219_I2C_ADDRESS, DC_LOAD_SHUNT_RATED_A, DC_LOAD_SHUNT_RATED_MV);
    Serial.println(initDcLoadPowerSensor() ? "[DC INA219] OK" : "[DC INA219] INDISPONIBLE");
#else
    Serial.println("[DC INA219] MESURE DESACTIVEE OU SHUNT NON CONFIGURE");
#endif

    // ZMPT101B + ACS712-30A
    // =================================================

    Serial.println();
    updateIndicators();
    Serial.println("[AC] Initialisation ZMPT101B + ACS712-30A...");
    if (initACPowerSensor())
    {
        Serial.println("[AC] Entrees ADC initialisees");
    }
    else
    {
        Serial.println("[AC] MESURE DESACTIVEE");
    }

    // =================================================
    // GPS
    // =================================================

    Serial.println();
    initGPS();
    initGeofence();
    updateIndicators();

    // =================================================
    // HORLOGE RTC DS1302
    // =================================================

    Serial.println();
    Serial.println("[DS1302] Initialisation...");

    if (initRTC())
    {
        Serial.println("[DS1302] HORLOGE OK");
    }
    else
    {
        Serial.println("[DS1302] ERREUR - MODE DE SECOURS ACTIF");
    }

    // =================================================
    // INTERNET
    // =================================================

#if DJUA_COMM_PROFILE == DJUA_PROFILE_SMS_PRODUCTION
    initSMS();
#else
    initInternet();
#if DJUA_COMM_PROFILE == DJUA_PROFILE_MQTT_TEST
    initMQTT();
#endif
#endif

    initDeviceStatus(isPowerSensorAvailable(), isRTCAvailable());
    updateCommunicationStatus();

    lastTelemetryTime = millis();
#if DJUA_COMM_PROFILE == DJUA_PROFILE_SMS_PRODUCTION
    lastTelemetryTime -= ELECTRICAL_SAMPLE_INTERVAL_MS; // premier point sans attendre 10 s
#endif
    lastGeofenceCheckTime = millis();

    Serial.println();
    Serial.println("[SYSTEM] Initialisation terminee.");
    setSystemIndicatorState(SYSTEM_RUNNING);
    updateIndicators();
}


// =====================================================
// LOOP
// =====================================================

void loop()
{
    // Toujours avant les retours anticipes et les operations synchrones.
    updateIndicators();
    // Permet l'etalonnage depuis le moniteur serie sans reflasher l'ESP32.
#if !D2_PROVISIONING_ENABLED || DJUA_COMM_PROFILE != DJUA_PROFILE_SMS_PRODUCTION
    updateACPowerCalibrationConsole();
#endif

    updateGPS();

    // Priorite a la surveillance locale avant les transports reseau.
    updateGeofenceMonitoring();

    
#if DJUA_COMM_PROFILE == DJUA_PROFILE_SMS_PRODUCTION
    updateSMS();
#else
    updateInternet();
#if DJUA_COMM_PROFILE == DJUA_PROFILE_MQTT_TEST
    updateMQTT();
#endif
#endif
    updateCommunicationStatus();
    updateIndicators();

    unsigned long now = millis();

    if (
        now - lastTelemetryTime <
        ELECTRICAL_SAMPLE_INTERVAL_MS
    )
    {
        return;
    }

    unsigned long elapsed =
        now - lastTelemetryTime;

    lastTelemetryTime = now;

    unsigned long intervalSeconds =
        elapsed / 1000;

    if (intervalSeconds == 0)
    {
        intervalSeconds =
            TELEMETRY_INTERVAL_MS / 1000;
    }

    Serial.println();
    Serial.println("=================================");
    Serial.println("        NOUVELLE MESURE");
    Serial.println("=================================");

    // =================================================
    // BATTERIE REELLE
    // =================================================

    PowerData battery =
        readPowerSensor();
    const uint32_t batteryAt = millis();

    Serial.print("[INA219 RAW] bus=");
    Serial.print(battery.busVoltage, 3);
    Serial.print(" V | shunt=");
    Serial.print(battery.shuntVoltageMv, 3);
    Serial.print(" mV | courant_capteur=");
    Serial.print(battery.sensorCurrentMa, 2);
    Serial.print(" mA | echantillons=");
    Serial.print(battery.sampleCount);
    Serial.print('/');
    Serial.print(INA219_SAMPLE_COUNT);
    Serial.print(" | recuperation=");
    Serial.println(battery.recoveryAttempted ? "oui" : "non");

    if (!battery.valid)
    {
        Serial.print("[INA219] Mesure invalide : tension attendue entre ");
        Serial.print(BATTERY_MIN_VALID_V, 1);
        Serial.print(" et ");
        Serial.print(BATTERY_MAX_VALID_V, 1);
        Serial.println(" V. La mesure AC continue.");
    }
    else
    {
        Serial.print("[BAT] Tension : ");
        Serial.print(battery.voltage, 2);
        Serial.println(" V");

        Serial.print("[BAT] Courant : ");
        Serial.print(battery.current, 3);
        Serial.println(" A");

        Serial.print("[BAT] Puissance : ");
        Serial.print(battery.power, 2);
        Serial.println(" W");
    }

    // =================================================
    // SOLAIRE REEL
    // =================================================

    SolarPowerData solar = readSolarPowerSensor();
    const uint32_t solarAt = millis();
    updateIndicators();
#if SOLAR_MEASUREMENT_ENABLED
    if (solar.valid)
    {
        Serial.printf("[SOLAR] Tension : %.2f V\n", solar.voltage);
        Serial.printf("[SOLAR] Courant : %.3f A\n", solar.current);
        Serial.printf("[SOLAR] Puissance : %.2f W\n", solar.power);
    }
    else
        Serial.println("[SOLAR] Mesure invalide");
#endif

    // =================================================
    // CHARGE AC REELLE
    // =================================================

    DcLoadPowerData dcLoad = readDcLoadPowerSensor();
    const uint32_t dcAt = millis();
    if (dcLoad.valid)
        Serial.printf("[DC LOAD] %.3f V | %.3f A | %.2f W\n", dcLoad.voltage, dcLoad.current, dcLoad.power);
#if DC_LOAD_MEASUREMENT_ENABLED && DC_LOAD_SHUNT_CONFIGURED
    else Serial.println("[DC LOAD] Mesure invalide");
#endif
    updateIndicators();
    ACPowerData acLoad = readACPowerSensor();
    const uint32_t acAt = millis();
    updateIndicators();

    if (acLoad.valid)
    {
        Serial.print("[AC] Tension RMS : ");
        Serial.print(acLoad.voltageRms, 1);
        Serial.println(" V");

        Serial.print("[AC] Courant RMS : ");
        Serial.print(acLoad.currentRms, 3);
        Serial.println(" A");

        Serial.print("[AC] Puissance active : ");
        Serial.print(acLoad.activePower, 1);
        Serial.println(" W");

        Serial.print("[AC] Puissance apparente : ");
        Serial.print(acLoad.apparentPower, 1);
        Serial.println(" VA");

        Serial.print("[AC] Facteur de puissance : ");
        Serial.println(acLoad.powerFactor, 3);
    }
    else
    {
        Serial.print("[AC] Mesure invalide | echantillons=");
        Serial.print(acLoad.sampleCount);
        Serial.print(" | duree_ms=");
        Serial.print(acLoad.acquisitionDurationMs);
        Serial.print(" | fenetre_complete=");
        Serial.print(acLoad.windowComplete ? "oui" : "non");
        Serial.print(" | bias V/I=");
        Serial.print(acLoad.voltageBiasMv, 1);
        Serial.print('/');
        Serial.print(acLoad.currentBiasMv, 1);
        Serial.print(" mV | ecretage V/I=");
        Serial.print(acLoad.voltageClipped ? "oui" : "non");
        Serial.print('/');
        Serial.println(acLoad.currentClipped ? "oui" : "non");
    }

    // =================================================
    // GPS REEL
    // =================================================

    updateGPS(); // vider les caracteres accumules pendant la fenetre AC
    GPSData location =
        readGPS();

    if (location.valid)
    {
        Serial.print("[GPS] Latitude : ");
        Serial.println(
            location.latitude,
            6
        );

        Serial.print("[GPS] Longitude : ");
        Serial.println(
            location.longitude,
            6
        );
    }
    else
    {
        Serial.println(
            "[GPS] Position non exploitable (absente en D2)"
        );
    }

    // =================================================
    // HORLOGE RTC DS1302
    // =================================================

    RTCData clock = readRTC();

    // Une seule decision d'alerte sur toutes les mesures du meme cycle.
    recordSolarStatus(isSolarPowerSensorAvailable(), solar.valid, true);
    recordDcLoadStatus(isDcLoadPowerSensorAvailable(), dcLoad.valid, true);
    recordSensorStatus(isPowerSensorAvailable(), battery.valid, acLoad.valid,
                       clock.valid, battery.voltage);
    updateIndicators();

    // =================================================
    // CONSTRUCTION TELEMETRIE
    // =================================================

    TelemetryData telemetry =
        buildTelemetry(
            battery,
            solar,
            acLoad,
            location,
            clock,
            intervalSeconds,
            dcLoad
        );

    if (telemetry.timestampValid)
    {
        Serial.print("[DS1302] Timestamp : ");
        Serial.println(telemetry.timestamp);
    }
    else
    {
        Serial.println(
            "[DS1302] Timestamp invalide - utilisation du secours."
        );
    }

    // =================================================
    // ENVOI MQTT
    // =================================================

#if DJUA_COMM_PROFILE == DJUA_PROFILE_SMS_PRODUCTION
    sampleD2Window(battery, solar, acLoad, dcLoad, batteryAt, solarAt, acAt, dcAt, location, clock);
#else
    bool mqttSuccess = false;
    bool httpSuccess = false;
    (void)batteryAt; (void)solarAt; (void)dcAt;
    dcWindow.sample(acAt, dcLoad.power, dcLoad.valid, D2_ENERGY_MAX_GAP_MS);
    if (dcLoad.valid && dcWindow.durationMs(acAt) > 0 && dcWindow.complete(acAt)) {
        telemetry.dcLoadEnergyIntervalWh = static_cast<float>(dcWindow.wattHours());
        telemetry.dcLoadEnergyComplete = true;
    }
    dcWindow.reset(acAt, dcLoad.power, dcLoad.valid);
#if DJUA_COMM_PROFILE == DJUA_PROFILE_MQTT_TEST
    mqttSuccess = sendTelemetryToMQTT(telemetry);
#endif

#if ENABLE_HTTP_BACKEND
    // =================================================
    // ENVOI BACKEND HTTP (optionnel)
    // =================================================

    httpSuccess = sendTelemetryToBackend(telemetry);
    if (httpSuccess)
    {
        Serial.println("[BACKEND] TELEMETRIE ACCEPTEE");
    }
    else
    {
        Serial.println("[BACKEND] ECHEC ENVOI");
    }
#endif
    recordTelemetryStatus(mqttSuccess, httpSuccess);
#endif
    updateCommunicationStatus();
    updateIndicators();
}
#endif // legacy hardware profile
