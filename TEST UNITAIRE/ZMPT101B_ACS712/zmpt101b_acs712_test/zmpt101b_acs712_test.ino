#include <Arduino.h>

// Le test inclut volontairement l'implementation de production afin de
// valider exactement le code utilise par iot_box.ino sans en maintenir une
// seconde copie. Arduino ne compile pas automatiquement les .cpp situes en
// dehors du dossier du sketch.
#define IOT_HARDWARE_V2 0
#include "../../../src/sensors/ac_power_sensor.cpp"

constexpr uint32_t TEST_REPORT_INTERVAL_MS = 2000UL;
static uint32_t lastReportMs = 0U;

void setup()
{
    Serial.begin(SERIAL_BAUD_RATE);
    delay(1000);

    Serial.println();
    Serial.println("=== TEST ZMPT101B + ACS712-30A : W / VA / PF ===");
    Serial.println("DANGER SECTEUR : couper l'alimentation avant tout cablage.");
    Serial.println("ZMPT OUT -> diviseur 10k/10k -> GPIO34");
    Serial.println("ACS OUT  -> diviseur 10k/10k -> GPIO35");
    Serial.println("ACS IP+ et IP- en serie dans la phase de la charge.");

    initACPowerSensor();
    lastReportMs = millis() - TEST_REPORT_INTERVAL_MS;
}

void loop()
{
    updateACPowerCalibrationConsole();

    if (millis() - lastReportMs < TEST_REPORT_INTERVAL_MS)
    {
        delay(5);
        return;
    }

    lastReportMs = millis();
    const ACPowerData measurement = readACPowerSensor();
    printACPowerMeasurement(measurement);
    Serial.println();
}
