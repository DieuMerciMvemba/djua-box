#ifndef POWER_SENSOR_H
#define POWER_SENSOR_H

#include <Arduino.h>

// =====================================================
// IOT BOX - INTERFACE CAPTEUR INA219
// =====================================================

// Structure contenant une mesure electrique complete
struct PowerData {
    float voltage;          // Tension VIN+ estimee, en volts
    float current;          // Courant en amperes
    float power;            // Puissance calculee, en watts
    float busVoltage;       // Lecture VIN- -> GND, avant calcul
    float shuntVoltageMv;   // Lecture VIN+ -> VIN-, en millivolts
    float sensorCurrentMa;  // Lecture courant retournee par le INA219
    uint8_t sampleCount;    // Nombre de triplets I2C valides filtres
    bool recoveryAttempted; // true si le pilote a relance le INA219
    bool valid;             // true si lecture et plage sont coherentes
};

// Initialise le capteur INA219.
// Retourne true si le capteur est detecte.
bool initPowerSensor();

// Lit les donnees du INA219.
// Retourne une structure PowerData.
PowerData readPowerSensor();

// Indique si le INA219 a ete correctement initialise.
bool isPowerSensorAvailable();

#endif
