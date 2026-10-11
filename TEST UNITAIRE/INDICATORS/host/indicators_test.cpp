#include <cassert>
#include <iostream>
#include "Arduino.h"
#include "esp_timer.h"
#define V2_BATTERY_CONFIGURED 1
#define V2_SOLAR_CONFIGURED 1
#define V2_DC_CONFIGURED 1
#define V2_TV_CONFIGURED 1
#define PZEM_HARDWARE_CONFIGURED 1
#include "../../../src/config.h"
#include "../../../src/pins.h"
#include "../../../src/indicators/indicators.cpp"
#include "../../../src/status/device_status.cpp"
#include "../../../src/geofencing/geofence.cpp"

static void tick(uint32_t now) {
    testNow = now;
    if (testTimerCallback) testTimerCallback(nullptr);
}
#if BUZZER_ENABLED
static bool sounding() {
#if BUZZER_PASSIVE
    return toneFrequency != 0;
#else
    return pinValues[BUZZER_PIN] == (BUZZER_ACTIVE_HIGH ? HIGH : LOW);
#endif
}
#endif
static size_t soundLogs() {
    size_t count = 0;
    for (const auto& log : Serial.logs)
        if (log.find("[BUZZER] Pattern") != std::string::npos) ++count;
    return count;
}
static void recordTestSensors(bool batteryAvailable, bool batteryValid, bool acValid,
                              bool rtcValid, float voltage) {
    const bool available[6] = {batteryAvailable, true, true, true, true, true};
    const bool valid[6] = {batteryValid, true, true, true, true, true};
    recordV2SensorStatus(available, valid, acValid, rtcValid, voltage);
}
static void resetStatus() {
    triggerBuzzer(BUZZER_NONE);
    tick(0);
    initDeviceStatus(true, true);
}

int main() {
    initIndicators();
#if BUZZER_ENABLED
    assert(!sounding());
#endif
    assert(pinValues[LED_SYSTEM_PIN] == HIGH);
    tick(200); updateIndicators();
    assert(pinValues[LED_SYSTEM_PIN] == LOW);
    tick(400); updateIndicators();
    assert(pinValues[LED_SYSTEM_PIN] == HIGH);
    setSystemIndicatorState(SYSTEM_RUNNING);
    tick(499); updateIndicators(); assert(pinValues[LED_SYSTEM_PIN] == HIGH);
    tick(500); updateIndicators(); assert(pinValues[LED_SYSTEM_PIN] == LOW);
    tick(2400); updateIndicators(); assert(pinValues[LED_SYSTEM_PIN] == HIGH);
    // Un setter identique ne redemarre pas le heartbeat.
    tick(2450); setSystemIndicatorState(SYSTEM_RUNNING);
    tick(2500); updateIndicators(); assert(pinValues[LED_SYSTEM_PIN] == LOW);
    setCommunicationIndicatorState(COMM_CONNECTING);
    updateIndicators(); assert(pinValues[LED_COMM_PIN] == HIGH);
    tick(3000); updateIndicators(); assert(pinValues[LED_COMM_PIN] == LOW);
    setCommunicationIndicatorState(COMM_TRANSPORT_PENDING);
    updateIndicators(); assert(pinValues[LED_COMM_PIN] == HIGH);
    tick(3150); updateIndicators(); assert(pinValues[LED_COMM_PIN] == LOW);
    setCommunicationIndicatorState(COMM_ONLINE);
    updateIndicators(); assert(pinValues[LED_COMM_PIN] == HIGH);
    setCommunicationIndicatorState(COMM_OFFLINE);
    updateIndicators(); assert(pinValues[LED_COMM_PIN] == LOW);
    setAlertIndicatorState(ALERT_WARNING);
    updateIndicators(); assert(pinValues[LED_ALERT_PIN] == HIGH);
    tick(3650); updateIndicators(); assert(pinValues[LED_ALERT_PIN] == LOW);
    setAlertIndicatorState(ALERT_CRITICAL);
    tick(3800); updateIndicators(); assert(pinValues[LED_ALERT_PIN] == LOW);
    setAlertIndicatorState(ALERT_GEOFENCE);
    updateIndicators(); assert(pinValues[LED_ALERT_PIN] == HIGH);
    tick(3950); updateIndicators(); assert(pinValues[LED_ALERT_PIN] == LOW);
    setAlertIndicatorState(ALERT_NONE);
    updateIndicators(); assert(pinValues[LED_ALERT_PIN] == LOW);
    tick(UINT32_MAX - 50); setSystemIndicatorState(SYSTEM_BOOTING);
    setSystemIndicatorState(SYSTEM_RUNNING);
    tick(48); updateIndicators(); assert(pinValues[LED_SYSTEM_PIN] == HIGH);
    tick(49); updateIndicators(); assert(pinValues[LED_SYSTEM_PIN] == LOW);

    resetStatus();
    updateDeviceStatus(false, false, false); assert(getCommunicationIndicatorState() == COMM_OFFLINE);
    updateDeviceStatus(false, true, false); assert(getCommunicationIndicatorState() == COMM_CONNECTING);
    updateDeviceStatus(true, false, false); assert(getCommunicationIndicatorState() == COMM_TRANSPORT_PENDING);
    tick(MQTT_WARNING_GRACE_MS - 1); updateDeviceStatus(true, false, false);
    assert(getAlertIndicatorState() == ALERT_NONE);
    tick(MQTT_WARNING_GRACE_MS); updateDeviceStatus(true, false, false);
#if DJUA_COMM_PROFILE == DJUA_PROFILE_HTTP_TEST
    assert(getAlertIndicatorState() == ALERT_NONE); // MQTT inactif n'est pas une panne
#else
    assert(getAlertIndicatorState() == ALERT_WARNING);
#endif
    tick(0); updateDeviceStatus(true, false, false);
#if DJUA_COMM_PROFILE == DJUA_PROFILE_HTTP_TEST
    assert(getAlertIndicatorState() == ALERT_NONE);
#else
    assert(getAlertIndicatorState() == ALERT_WARNING);
#endif
    tick(MQTT_WARNING_GRACE_MS);
    updateDeviceStatus(true, false, true); assert(getAlertIndicatorState() == ALERT_NONE);
    assert(getCommunicationIndicatorState() == COMM_ONLINE);
    recordTelemetryStatus(false, true);
#if ENABLE_HTTP_BACKEND
    updateDeviceStatus(true, false, false); assert(getCommunicationIndicatorState() == COMM_ONLINE);
#else
    updateDeviceStatus(true, false, false); assert(getCommunicationIndicatorState() == COMM_TRANSPORT_PENDING);
#endif
    tick(MQTT_WARNING_GRACE_MS + HTTP_SUCCESS_VALID_MS);
    updateDeviceStatus(true, false, false); assert(getCommunicationIndicatorState() == COMM_TRANSPORT_PENDING);
    recordTelemetryStatus(false, true);
    updateDeviceStatus(false, false, false);
    updateDeviceStatus(true, false, false); assert(getCommunicationIndicatorState() == COMM_TRANSPORT_PENDING);

    resetStatus();
    updateDeviceStatus(true, false, true);
    recordTelemetryStatus(false, false);
    updateDeviceStatus(true, false, true);
    assert(getCommunicationIndicatorState() == COMM_TRANSPORT_PENDING);
    assert(getAlertIndicatorState() == ALERT_NONE);
    recordTelemetryStatus(true, false);
    updateDeviceStatus(true, false, true);
    assert(getCommunicationIndicatorState() == COMM_ONLINE);

    resetStatus();
    recordTelemetryStatus(false, false); assert(getAlertIndicatorState() == ALERT_NONE);
    recordTelemetryStatus(false, false); assert(getAlertIndicatorState() == ALERT_NONE);
    recordTelemetryStatus(false, false); assert(getAlertIndicatorState() == ALERT_WARNING);
    const size_t warnings = soundLogs();
    recordTelemetryStatus(false, false); assert(soundLogs() == warnings);
    recordTelemetryStatus(true, false); assert(getAlertIndicatorState() == ALERT_NONE);

    // Production SMS : aucune dependance Wi-Fi/MQTT, distinction pending/submit,
    // alerte de persistance et priorite geofence toujours locale.
    resetStatus();
    updateSMSDeviceStatus(false, false, false, false, false);
    assert(getCommunicationIndicatorState() == COMM_OFFLINE);
    updateSMSDeviceStatus(false, true, false, true, false);
    assert(getCommunicationIndicatorState() == COMM_CONNECTING);
    updateSMSDeviceStatus(true, false, false, true, false);
    assert(getCommunicationIndicatorState() == COMM_TRANSPORT_PENDING);
    recordSMSTelemetryStatus(true);
    updateSMSDeviceStatus(true, false, true, false, false);
    assert(getCommunicationIndicatorState() == COMM_ONLINE && getAlertIndicatorState() == ALERT_NONE);
    updateSMSDeviceStatus(true, false, true, false, true);
    assert(getAlertIndicatorState() == ALERT_WARNING);
    recordGeofenceStatus(true, false);
    assert(getAlertIndicatorState() == ALERT_GEOFENCE);

    resetStatus();
    for (int i = 0; i < 2; ++i) recordTestSensors(true, false, true, true, 0);
    assert(getAlertIndicatorState() == ALERT_NONE);
    recordTestSensors(true, false, true, true, 0); assert(getAlertIndicatorState() == ALERT_WARNING);
    recordTestSensors(true, true, true, true, 12); assert(getAlertIndicatorState() == ALERT_NONE);
    for (int i = 0; i < 3; ++i) recordTestSensors(false, false, true, true, 0);
    assert(getAlertIndicatorState() == ALERT_CRITICAL);
    recordTestSensors(false, false, true, false, 0); assert(getAlertIndicatorState() == ALERT_CRITICAL);
    recordGeofenceStatus(true, false); assert(getAlertIndicatorState() == ALERT_GEOFENCE);
    const size_t exits = soundLogs();
    recordGeofenceStatus(true, false); assert(soundLogs() == exits);
    recordTestSensors(false, false, false, false, 0); assert(getAlertIndicatorState() == ALERT_GEOFENCE);
    recordGeofenceStatus(false, true); assert(getAlertIndicatorState() == ALERT_CRITICAL);
    const size_t enters = soundLogs();
    recordGeofenceStatus(false, true); assert(soundLogs() == enters);

    resetStatus();
    for (int i = 0; i < 3; ++i) recordTestSensors(true, false, false, true, 0);
    assert(getAlertIndicatorState() == ALERT_CRITICAL);
    recordTestSensors(true, true, true, false, 12); assert(getAlertIndicatorState() == ALERT_WARNING);
    recordTestSensors(true, true, true, true, 1); assert(getAlertIndicatorState() == ALERT_NONE);
    // Les nouvelles voies sont diagnostiquees via le meme cycle groupe.
    bool present[6] = {true, true, true, true, true, true};
    bool valid[6] = {true, true, true, true, true, true};
    for (unsigned failed : {1U, 2U, 3U, 4U, 5U}) {
        resetStatus(); valid[failed] = false;
        for (unsigned n = 0; n < SENSOR_WARNING_FAILURE_COUNT; ++n)
            recordV2SensorStatus(present, valid, true, true, 12);
        assert(getAlertIndicatorState() == ALERT_WARNING);
        valid[failed] = true;
        recordV2SensorStatus(present, valid, true, true, 12);
        assert(getAlertIndicatorState() == ALERT_NONE);
    }
    present[1] = false;
    for (unsigned n = 0; n < SENSOR_WARNING_FAILURE_COUNT; ++n)
        recordV2SensorStatus(present, valid, true, true, 12);
    assert(getAlertIndicatorState() == ALERT_WARNING);
    present[1] = true;
    recordV2SensorStatus(present, valid, true, true, 12);
    assert(getAlertIndicatorState() == ALERT_NONE);
    // Integration avec les transitions reelles du geofencing, sans GPS simule valide.
    initGeofence();
    GPSData position = {};
    GeofenceResult result = updateGeofence(position);
    recordGeofenceStatus(result.exitEvent, result.enterEvent);
    assert(getAlertIndicatorState() == ALERT_NONE);
    position = {GEOFENCE_CENTER_LAT, GEOFENCE_CENTER_LON, true};
    updateGeofence(position);
    position.latitude += 0.002;
    for (unsigned i = 0; i < GEOFENCE_CONFIRM_COUNT; ++i) {
        result = updateGeofence(position);
        recordGeofenceStatus(result.exitEvent, result.enterEvent);
        assert(getAlertIndicatorState() == (i + 1 == GEOFENCE_CONFIRM_COUNT ? ALERT_GEOFENCE : ALERT_NONE));
    }
    position.valid = false;
    result = updateGeofence(position);
    recordGeofenceStatus(result.exitEvent, result.enterEvent);
    assert(getAlertIndicatorState() == ALERT_GEOFENCE);
    position = {GEOFENCE_CENTER_LAT, GEOFENCE_CENTER_LON, true};
    for (unsigned i = 0; i < GEOFENCE_CONFIRM_COUNT; ++i) {
        result = updateGeofence(position);
        recordGeofenceStatus(result.exitEvent, result.enterEvent);
    }
    assert(getAlertIndicatorState() == ALERT_NONE);

#if BUZZER_ENABLED
    // Horloge du timer seule : la boucle LED reste arretee durant les sons.
    for (int event = BUZZER_INFO; event <= BUZZER_CRITICAL; ++event) {
        triggerBuzzer(BUZZER_NONE); tick(0);
        triggerBuzzer(static_cast<BuzzerEvent>(event)); tick(0);
        for (int beep = 0; beep < event; ++beep) {
            tick(beep * 250); assert(sounding());
            tick(beep * 250 + 99); assert(sounding());
            tick(beep * 250 + 100); assert(!sounding());
        }
        tick(2000); assert(!sounding());
    }
    triggerBuzzer(BUZZER_GEOFENCE); tick(3000); assert(sounding());
    triggerBuzzer(BUZZER_WARNING); // Ne doit pas ecraser la geofence.
    tick(3699); assert(sounding());
    tick(3700); assert(!sounding());
    tick(3900); assert(sounding());
    tick(4050); assert(!sounding());
    tick(4200); assert(sounding());
    tick(4350); assert(!sounding());
    tick(999999); assert(!sounding());
    triggerBuzzer(BUZZER_INFO); tick(UINT32_MAX - 49); assert(sounding());
    tick(50); assert(!sounding());
    triggerBuzzer(BUZZER_GEOFENCE); tick(100); assert(sounding());
    triggerBuzzer(BUZZER_NONE); tick(110); assert(!sounding());
#else
    assert(pinModes[BUZZER_PIN] == 0);
    assert(testTimerCallback == nullptr);
    assert(soundLogs() == 0);
#endif
    std::cout << "PASS: LED timings, rollover, communication, sensor recovery, telemetry, geofence, buzzer\n";
}
