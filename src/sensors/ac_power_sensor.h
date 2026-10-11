#ifndef AC_POWER_SENSOR_H
#define AC_POWER_SENSOR_H

#include <Arduino.h>

// Mesure AC complete. La puissance active provient de la moyenne des
// produits instantanes v(t) * i(t), et non du simple produit des RMS.
struct ACPowerData
{
    float voltageRms;
    float currentRms;
    float currentPeak;
    float activePower;
    float apparentPower;
    float powerFactor;

    float voltageSignalRmsMv;
    float currentSignalRmsMv;
    float voltageBiasMv;
    float currentBiasMv;
    uint32_t sampleCount;
    uint32_t acquisitionDurationMs;

    bool windowComplete;
    bool voltageBiasValid;
    bool currentBiasValid;
    bool voltageClipped;
    bool currentClipped;
    bool currentOverRange;
    bool enoughSamples;
    bool valid;
};

// Initialise les ADC1 et charge la calibration conservee en memoire NVS.
bool initACPowerSensor();

// Effectue une acquisition synchronisee ZMPT101B/ACS712.
ACPowerData readACPowerSensor();

bool isACPowerSensorAvailable();

// Console non bloquante de calibration sur le port Serial USB.
// Les commandes reconnues commencent par "AC CAL".
void updateACPowerCalibrationConsole();
void printACPowerCalibration();
void printACPowerMeasurement(const ACPowerData& value);

#endif
