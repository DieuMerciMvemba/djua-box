#include <Arduino.h>
#include "device_status.h"
#include "../config.h"
#include "../indicators/indicators.h"

namespace {
uint32_t powerFailures = 0, powerUnavailable = 0, acFailures = 0;
uint32_t solarFailures = 0, solarUnavailable = 0;
uint32_t dcFailures = 0, dcUnavailable = 0;
uint32_t telemetryFailures = 0;
bool rtcInvalid = false, geofenceAlarmActive = false;
bool geofenceEnterHandled = false;
bool batteryWarning = false, batteryCritical = false;
bool mqttMissing = false, mqttWarning = false;
bool mqttPublishFailed = false;
bool smsWarning = false;
uint32_t mqttMissingSince = 0;
bool httpHealthy = false;
uint32_t lastHttpSuccess = 0;
AlertIndicatorState lastAlert = ALERT_NONE;

static_assert(SENSOR_WARNING_FAILURE_COUNT > 0 &&
              INA219_CRITICAL_FAILURE_COUNT > 0 &&
              TELEMETRY_WARNING_FAILURE_COUNT > 0, "Seuils de compteurs invalides");
#if BATTERY_ALERTS_ENABLED
static_assert(BATTERY_CRITICAL_V > 0 && BATTERY_WARNING_V > BATTERY_CRITICAL_V,
              "Seuils metier batterie invalides");
#endif

void countFailure(uint32_t& counter, bool failed) {
    if (!failed) counter = 0;
    else if (counter < UINT32_MAX) ++counter;
}

void refreshAlert(bool announce = true) {
    const bool powerWarning = powerFailures >= SENSOR_WARNING_FAILURE_COUNT;
    const bool acWarning = (IOT_HARDWARE_V2 ? PZEM_HARDWARE_CONFIGURED : AC_MEASUREMENT_ENABLED) && acFailures >= SENSOR_WARNING_FAILURE_COUNT;
    const bool solarWarning = (IOT_HARDWARE_V2 ? V2_SOLAR_CONFIGURED : SOLAR_MEASUREMENT_ENABLED) &&
        (solarFailures >= SENSOR_WARNING_FAILURE_COUNT ||
         solarUnavailable >= SENSOR_WARNING_FAILURE_COUNT);
    const bool critical = powerUnavailable >= INA219_CRITICAL_FAILURE_COUNT ||
                          (powerWarning && acWarning) || batteryCritical;
    const bool dcWarning = (IOT_HARDWARE_V2 ? (V2_DC_CONFIGURED || V2_TV_CONFIGURED) :
        (DC_LOAD_MEASUREMENT_ENABLED && DC_LOAD_SHUNT_CONFIGURED)) &&
        (dcFailures >= SENSOR_WARNING_FAILURE_COUNT || dcUnavailable >= SENSOR_WARNING_FAILURE_COUNT);
    const bool warning = rtcInvalid || powerWarning || acWarning || solarWarning || dcWarning || batteryWarning ||
                         mqttWarning || smsWarning || telemetryFailures >= TELEMETRY_WARNING_FAILURE_COUNT;
    const AlertIndicatorState next = geofenceAlarmActive ? ALERT_GEOFENCE :
        critical ? ALERT_CRITICAL : warning ? ALERT_WARNING : ALERT_NONE;
    setAlertIndicatorState(next);
    if (announce && next != lastAlert) {
        if (next == ALERT_GEOFENCE) triggerBuzzer(BUZZER_GEOFENCE);
        else if (next == ALERT_CRITICAL) triggerBuzzer(BUZZER_CRITICAL);
        else if (next == ALERT_WARNING) triggerBuzzer(BUZZER_WARNING);
    }
    lastAlert = next;
}
} // namespace

void initDeviceStatus(bool powerAvailable, bool rtcValid) {
    powerFailures = powerAvailable ? 0 : 1;
    powerUnavailable = powerFailures;
    acFailures = telemetryFailures = 0;
    solarFailures = solarUnavailable = 0;
    dcFailures = dcUnavailable = 0;
    rtcInvalid = !rtcValid;
    geofenceAlarmActive = batteryWarning = batteryCritical = false;
    geofenceEnterHandled = false;
    mqttMissing = mqttWarning = httpHealthy = false;
    mqttPublishFailed = false;
    smsWarning = false;
    lastAlert = ALERT_NONE;
    refreshAlert();
}

void recordV2SensorStatus(const bool sensorPresent[6], const bool valid[6], bool acValid,
                          bool rtcValid, float batteryVoltage) {
    countFailure(powerFailures, V2_BATTERY_CONFIGURED && !valid[0]);
    countFailure(powerUnavailable, V2_BATTERY_CONFIGURED && !sensorPresent[0]);
    countFailure(solarFailures, V2_SOLAR_CONFIGURED && !valid[1]);
    countFailure(solarUnavailable, V2_SOLAR_CONFIGURED && !sensorPresent[1]);
    bool dcFailed = false, dcMissing = false;
    for (unsigned i = 2; i < 6; ++i) {
        const bool enabled = i == 5 ? V2_TV_CONFIGURED : V2_DC_CONFIGURED;
        dcFailed |= enabled && !valid[i]; dcMissing |= enabled && !sensorPresent[i];
    }
    countFailure(dcFailures, dcFailed); countFailure(dcUnavailable, dcMissing);
    countFailure(acFailures, PZEM_HARDWARE_CONFIGURED && !acValid); rtcInvalid = !rtcValid;
#if BATTERY_ALERTS_ENABLED
    if (V2_BATTERY_CONFIGURED && valid[0]) {
        batteryWarning = batteryVoltage <= BATTERY_WARNING_V; batteryCritical = batteryVoltage <= BATTERY_CRITICAL_V;
    }
#else
    (void)batteryVoltage;
#endif
    refreshAlert();
}
void recordSolarStatus(bool solarAvailable, bool solarValid, bool deferAlertRefresh) {
    countFailure(solarFailures, SOLAR_MEASUREMENT_ENABLED && !solarValid);
    countFailure(solarUnavailable, SOLAR_MEASUREMENT_ENABLED && !solarAvailable);
    if (SOLAR_MEASUREMENT_ENABLED && (!solarAvailable || !solarValid))
        Serial.printf("[SOLAR STATUS] echecs=%lu | indisponible=%lu\n",
                      static_cast<unsigned long>(solarFailures),
                      static_cast<unsigned long>(solarUnavailable));
    if (!deferAlertRefresh) refreshAlert();
}

void recordDcLoadStatus(bool sensorAvailable, bool valid, bool deferAlertRefresh) {
    const bool enabled = DC_LOAD_MEASUREMENT_ENABLED && DC_LOAD_SHUNT_CONFIGURED;
    countFailure(dcFailures, enabled && !valid);
    countFailure(dcUnavailable, enabled && !sensorAvailable);
    if (enabled && (!sensorAvailable || !valid))
        Serial.printf("[DC STATUS] echecs=%lu | indisponible=%lu\n",
            static_cast<unsigned long>(dcFailures), static_cast<unsigned long>(dcUnavailable));
    if (!deferAlertRefresh) refreshAlert();
}

void recordSensorStatus(bool powerAvailable, bool powerValid, bool acValid,
                        bool rtcValid, float batteryVoltage) {
    countFailure(powerFailures, !powerValid);
    countFailure(powerUnavailable, !powerAvailable);
    countFailure(acFailures, AC_MEASUREMENT_ENABLED && !acValid);
    rtcInvalid = !rtcValid;
#if BATTERY_ALERTS_ENABLED
    // Une mesure invalide ne peut ni declencher ni effacer un seuil metier.
    if (powerValid) {
        batteryWarning = batteryVoltage <= BATTERY_WARNING_V;
        batteryCritical = batteryVoltage <= BATTERY_CRITICAL_V;
    }
#else
    (void)batteryVoltage;
#endif
    refreshAlert();
}

void recordGeofenceStatus(bool exitEvent, bool enterEvent) {
    if (exitEvent && !geofenceAlarmActive) {
        geofenceAlarmActive = true;
        geofenceEnterHandled = false;
        refreshAlert();
    } else if (enterEvent && !geofenceEnterHandled) {
        // ENTER peut aussi suivre un demarrage directement hors zone, pour
        // lequel le geofencing existant n'emet pas EXIT. Appel sur evenements
        // seulement : une transition confirmee donne exactement un INFO.
        geofenceAlarmActive = false;
        geofenceEnterHandled = true;
        refreshAlert(false);
        triggerBuzzer(BUZZER_NONE);
        triggerBuzzer(BUZZER_INFO);
    }
}

void recordTelemetryStatus(bool mqttSuccess, bool httpSuccess) {
    mqttPublishFailed = !mqttSuccess;
    countFailure(telemetryFailures, !mqttSuccess && !httpSuccess);
#if ENABLE_HTTP_BACKEND
    httpHealthy = httpSuccess;
    if (httpSuccess) lastHttpSuccess = millis();
#else
    (void)httpSuccess;
#endif
    refreshAlert();
}

void updateDeviceStatus(bool networkAvailable, bool networkConnecting, bool mqttConnected) {
    const uint32_t now = millis();
    if (!networkAvailable || (httpHealthy && now - lastHttpSuccess >= HTTP_SUCCESS_VALID_MS))
        httpHealthy = false;

    const bool mqttUsable = mqttConnected && !mqttPublishFailed;
#if DJUA_COMM_PROFILE == DJUA_PROFILE_HTTP_TEST
    const bool monitorMQTT = false;
#else
    const bool monitorMQTT = true;
#endif
    if (monitorMQTT && networkAvailable && !mqttUsable) {
        if (!mqttMissing) {
            mqttMissing = true;
            mqttMissingSince = now;
        }
        // Une fois confirmee, l'anomalie reste active meme apres un tour
        // complet de millis(), jusqu'au retablissement du transport/reseau.
        mqttWarning = mqttWarning || now - mqttMissingSince >= MQTT_WARNING_GRACE_MS;
    } else {
        mqttMissing = mqttWarning = false;
    }
    setCommunicationIndicatorState(!networkAvailable
        ? (networkConnecting ? COMM_CONNECTING : COMM_OFFLINE)
        : (mqttUsable || httpHealthy ? COMM_ONLINE : COMM_TRANSPORT_PENDING));
    refreshAlert();
}

void recordSMSTelemetryStatus(bool acceptedByModem) {
    countFailure(telemetryFailures, !acceptedByModem);
    refreshAlert();
}

void updateSMSDeviceStatus(bool registered, bool connecting, bool healthy, bool pending, bool fault) {
    // Aucun diagnostic Wi-Fi/MQTT en production SMS. ONLINE indique uniquement
    // le transport modem disponible, jamais la confirmation du backend.
    mqttMissing = mqttWarning = httpHealthy = false;
    smsWarning = fault;
    setCommunicationIndicatorState(!registered
        ? (connecting ? COMM_CONNECTING : COMM_OFFLINE)
        : (healthy && !pending ? COMM_ONLINE : COMM_TRANSPORT_PENDING));
    refreshAlert();
}
