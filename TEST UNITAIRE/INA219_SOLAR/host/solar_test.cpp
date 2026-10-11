#include <cassert>
#include <cmath>
#include <iostream>
#include "Adafruit_INA219.h"
#include "../../INDICATORS/host/esp_timer.h"
#include "../../../src/sensors/solar_power_sensor.cpp"
#include "../../../src/telemetry/telemetry.cpp"
#include "../../../src/indicators/indicators.cpp"
#include "../../../src/status/device_status.cpp"

bool formatRTCTimestamp(const RTCData&, char* output, size_t) {
    output[0] = '\0'; return false;
}
#if SOLAR_MEASUREMENT_ENABLED
static void reset() {
    testPresent = testCalibrationOk = testRegisterOk = true;
    testOverflow = false;
    testSamples.clear(); sampleIndex = 0; defaultSample = TestSample{};
}
#endif
static size_t buzzerLogs() {
    size_t count = 0;
    for (const auto& log : Serial.logs)
        if (log.find("[BUZZER] Pattern WARNING") != std::string::npos) ++count;
    return count;
}
int main() {
    initIndicators();
    initDeviceStatus(true, true);
    setSystemIndicatorState(SYSTEM_RUNNING);
    updateDeviceStatus(true, false, true);
#if SOLAR_MEASUREMENT_ENABLED
    reset(); assert(initSolarPowerSensor());
    auto solar = readSolarPowerSensor();
    assert(solar.valid && solar.sampleCount == 7 && !solar.recoveryAttempted);
    assert(std::fabs(solar.voltage - 18.1F) < 0.001F);
    assert(solar.current == SOLAR_CURRENT_POLARITY);
    PowerData battery = {}; battery.valid = true; battery.voltage = 12.5F;
    ACPowerData ac = {}; ac.valid = true; ac.activePower = 100;
    GPSData gps = {}; gps.valid = true; gps.latitude = 1.5;
    auto telemetry = buildTelemetry(battery, solar, ac, gps, RTCData{}, 1800);
    assert(telemetry.solarMeasurementValid);
    assert(std::fabs(telemetry.solarEnergyIntervalWh - solar.power / 2) < 0.001F);
    assert(telemetry.batteryVoltage == 12.5F && telemetry.acEnergyIntervalWh == 50);
    assert(telemetry.latitude == 1.5);

    // Zero, courant negatif et polarite : jamais de valeur absolue masquant le signe.
    reset(); defaultSample.shunt = defaultSample.current = 0;
    solar = readSolarPowerSensor(); assert(solar.valid && solar.power == 0);
    telemetry = buildTelemetry(battery, solar, ac, gps, RTCData{}, 10);
    assert(telemetry.solarMeasurementValid && telemetry.solarEnergyIntervalWh == 0);
    recordSolarStatus(true, solar.valid);
    assert(getAlertIndicatorState() == ALERT_NONE && buzzerLogs() == 0);
    defaultSample.bus = 0;
    solar = readSolarPowerSensor(); assert(solar.valid && solar.voltage == 0 && solar.power == 0);
    defaultSample.bus = 18;
    defaultSample.current = -100; defaultSample.shunt = -10;
    solar = readSolarPowerSensor(); assert(solar.valid);
    assert(std::fabs(solar.current - (-0.1F * SOLAR_CURRENT_POLARITY)) < 0.0001F);

    // Outlier et lectures partiellement invalides avant mediane (6 puis 4 valides).
    reset(); testSamples.assign(7, defaultSample);
    testSamples[0].bus = 24; testSamples[1].shuntOk = false;
    solar = readSolarPowerSensor(); assert(solar.valid && solar.sampleCount == 6);
    assert(std::fabs(solar.voltage - 18.1F) < 0.001F);
    sampleIndex = 0; testSamples[2].bus = NAN; testSamples[3].bus = 27;
    solar = readSolarPowerSensor(); assert(solar.valid && solar.sampleCount == 4);

    // Fenetre invalide recuperee dans le meme cycle : pas d'alerte.
    reset(); testSamples.assign(7, defaultSample);
    for (auto& s : testSamples) s.busOk = false;
    solar = readSolarPowerSensor(); assert(solar.valid && solar.recoveryAttempted);

    // Chaque type d'invalidite persistante reste invalide apres recovery.
    for (int fault = 0; fault < 7; ++fault) {
        reset();
        if (fault == 0) defaultSample.bus = NAN;
        if (fault == 1) defaultSample.bus = 27;
        if (fault == 2) defaultSample.shunt = 320;
        if (fault == 3) defaultSample.current = 3200;
        if (fault == 4) testOverflow = true;
        if (fault == 5) testCalibrationOk = false;
        if (fault == 6) testRegisterOk = false;
        solar = readSolarPowerSensor(); assert(!solar.valid && solar.recoveryAttempted);
        telemetry = buildTelemetry(battery, solar, ac, gps, RTCData{}, 10);
        assert(!telemetry.solarMeasurementValid && std::isnan(telemetry.solarPower));
        assert(std::isnan(telemetry.solarEnergyIntervalWh));
        reset(); assert(initSolarPowerSensor());
    }

    // Une erreur puis retour valide annule le compteur.
    recordSolarStatus(true, false); assert(getAlertIndicatorState() == ALERT_NONE);
    assert(solarFailures == 1);
    recordSolarStatus(true, true); assert(solarFailures == 0);

    // Deconnexion -> WARNING a 3 cycles -> aucune repetition du buzzer.
    testPresent = false;
    const size_t before = buzzerLogs();
    for (unsigned cycle = 1; cycle <= SENSOR_WARNING_FAILURE_COUNT + 2; ++cycle) {
        // Finir le pattern precedent : un declenchement errone au cycle suivant
        // serait alors accepte et detecte (pas masque par un buzzer deja occupe).
        testNow += 10000;
        if (testTimerCallback) testTimerCallback(nullptr);
        const int begins = sensorBegins;
        solar = readSolarPowerSensor();
        assert(!solar.valid && !isSolarPowerSensorAvailable());
        assert(sensorBegins == begins + 1);
        recordSolarStatus(isSolarPowerSensorAvailable(), solar.valid);
        assert(getAlertIndicatorState() == (cycle < SENSOR_WARNING_FAILURE_COUNT
            ? ALERT_NONE : ALERT_WARNING));
        if (testTimerCallback) testTimerCallback(nullptr);
        updateIndicators();
        assert(getSystemIndicatorState() == SYSTEM_RUNNING);
        assert(getCommunicationIndicatorState() == COMM_ONLINE);
    }
    assert(buzzerLogs() == before + (BUZZER_ENABLED ? 1 : 0));
    assert(pinValues[LED_ALERT_PIN] == HIGH);
    testPresent = true; solar = readSolarPowerSensor();
    assert(solar.valid && solar.recoveryAttempted);
    recordSolarStatus(true, true); updateIndicators();
    assert(getAlertIndicatorState() == ALERT_NONE && pinValues[LED_ALERT_PIN] == LOW);
    assert(solarFailures == 0 && solarUnavailable == 0);

    // Presence sans validite : WARNING aussi. Recuperation conserve une autre alerte.
    for (unsigned i = 0; i < SENSOR_WARNING_FAILURE_COUNT; ++i) recordSolarStatus(true, false);
    assert(getAlertIndicatorState() == ALERT_WARNING && solarUnavailable == 0);
    recordSensorStatus(true, true, true, false, 12);
    recordSolarStatus(true, true); assert(getAlertIndicatorState() == ALERT_WARNING);
    for (unsigned i = 0; i < INA219_CRITICAL_FAILURE_COUNT; ++i)
        recordSensorStatus(false, false, true, true, 0);
    recordSolarStatus(false, false); assert(getAlertIndicatorState() == ALERT_CRITICAL);
    recordGeofenceStatus(true, false);
    recordSolarStatus(true, true); assert(getAlertIndicatorState() == ALERT_GEOFENCE);
    recordSolarStatus(false, false); assert(getAlertIndicatorState() == ALERT_GEOFENCE);

    // Recuperation simultanee batterie/solaire : aucun WARNING intermediaire.
    triggerBuzzer(BUZZER_NONE);
    if (testTimerCallback) testTimerCallback(nullptr);
    initDeviceStatus(true, true);
    for (unsigned i = 0; i < INA219_CRITICAL_FAILURE_COUNT; ++i)
        recordSensorStatus(false, false, true, true, 0);
    for (unsigned i = 0; i < SENSOR_WARNING_FAILURE_COUNT; ++i)
        recordSolarStatus(false, false);
    assert(getAlertIndicatorState() == ALERT_CRITICAL);
    triggerBuzzer(BUZZER_NONE);
    if (testTimerCallback) testTimerCallback(nullptr);
    const size_t beforeRecovery = buzzerLogs();
    recordSolarStatus(true, true, true);
    recordSensorStatus(true, true, true, true, 12);
    assert(getAlertIndicatorState() == ALERT_NONE && buzzerLogs() == beforeRecovery);

    // Un WARNING solaire remplace par un WARNING RTC reste le meme niveau.
    for (unsigned i = 0; i < SENSOR_WARNING_FAILURE_COUNT; ++i)
        recordSolarStatus(true, false);
    if (testTimerCallback) testTimerCallback(nullptr);
    testNow += 1000;
    if (testTimerCallback) testTimerCallback(nullptr);
    const size_t beforeHandover = buzzerLogs();
    recordSolarStatus(true, true, true);
    recordSensorStatus(true, true, true, false, 12);
    assert(getAlertIndicatorState() == ALERT_WARNING && buzzerLogs() == beforeHandover);
#else
    assert(!initSolarPowerSensor() && !isSolarPowerSensorAvailable());
    for (unsigned i = 0; i < 10; ++i) {
        const auto solar = readSolarPowerSensor();
        assert(!solar.valid && !solar.recoveryAttempted);
        recordSolarStatus(false, false);
        assert(getAlertIndicatorState() == ALERT_NONE);
    }
    assert(sensorBegins == 0 && buzzerLogs() == 0);
#endif
    assert(wireBegins == 0);
    std::cout << "PASS: solar driver, telemetry, failures/recovery, priorities, LEDs/buzzer\n";
}
