#include "ac_power_sensor.h"

#include <Preferences.h>
#include <math.h>
#include <stdlib.h>

#include "../config.h"
#include "../pins.h"

namespace
{
constexpr float ACS712_MAX_SINE_RMS_A = 30.0F / 1.41421356237F;
constexpr uint32_t ADC_CLIP_LOW_MV = 100U;
constexpr uint32_t ADC_CLIP_HIGH_MV = 2400U;
constexpr float BIAS_MIN_MV = 700.0F;
constexpr float BIAS_MAX_MV = 1800.0F;
constexpr float ABSENT_VOLTAGE_THRESHOLD_V = 5.0F;
constexpr int32_t MAX_PHASE_CORRECTION_US = 5000;
constexpr int32_t AUTO_PHASE_LIMIT_US = 2500;
constexpr int32_t AUTO_PHASE_STEP_US = 25;
constexpr uint8_t CALIBRATION_VERSION = 1U;
constexpr char NVS_NAMESPACE[] = "ac_cal";

static_assert(ZMPT101B_ADC_PIN != ACS712_ADC_PIN,
              "Les capteurs AC doivent utiliser deux GPIO distincts.");
static_assert(AC_OUTPUT_DIVIDER_RATIO > 0.0F &&
                  AC_OUTPUT_DIVIDER_RATIO < 1.0F,
              "Le rapport du diviseur AC doit etre entre 0 et 1.");
static_assert(AC_MAXIMUM_SAMPLE_COUNT >= AC_MINIMUM_SAMPLE_COUNT,
              "Le tampon AC est trop petit.");
static_assert(AC_SAMPLE_PERIOD_US > 0U,
              "La periode d'echantillonnage AC doit etre positive.");
static_assert(
    ((AC_MEASUREMENT_WINDOW_MS * 1000UL) + AC_SAMPLE_PERIOD_US - 1UL) /
        AC_SAMPLE_PERIOD_US < AC_MAXIMUM_SAMPLE_COUNT,
    "Le tampon AC ne peut pas contenir toute la fenetre configuree."
);

struct Calibration
{
    float voltageGain;
    float currentGain;
    float noiseFloorA;
    int32_t phaseCorrectionUs;
    int8_t currentPolarity;
};

struct SampleBuffer
{
    uint16_t voltageMv[AC_MAXIMUM_SAMPLE_COUNT];
    uint16_t currentMv[AC_MAXIMUM_SAMPLE_COUNT];
    uint32_t voltageTimeUs[AC_MAXIMUM_SAMPLE_COUNT];
    uint32_t currentTimeUs[AC_MAXIMUM_SAMPLE_COUNT];
    uint32_t count;
    uint32_t durationUs;
    bool windowComplete;
};

Calibration calibration = {};
SampleBuffer samples = {};
ACPowerData lastMeasurement = {};
bool sensorAvailable = false;
char consoleLine[96] = {};
size_t consoleLength = 0U;

Calibration defaultCalibration()
{
    Calibration value = {};
    value.voltageGain = ZMPT101B_VOLTAGE_GAIN_V_PER_ADC_MV;
    value.currentGain = AC_CURRENT_GAIN_A_PER_ADC_MV;
    value.noiseFloorA = AC_CURRENT_NOISE_FLOOR_A;
    value.phaseCorrectionUs = AC_PHASE_CORRECTION_US;
    value.currentPolarity = AC_CURRENT_POLARITY >= 0 ? 1 : -1;
    return value;
}

bool calibrationIsPlausible(const Calibration& value)
{
    return isfinite(value.voltageGain) &&
           value.voltageGain >= 0.01F && value.voltageGain <= 10.0F &&
           isfinite(value.currentGain) &&
           value.currentGain >= 0.001F && value.currentGain <= 1.0F &&
           isfinite(value.noiseFloorA) &&
           value.noiseFloorA >= 0.005F && value.noiseFloorA <= 5.0F &&
           value.phaseCorrectionUs >= -MAX_PHASE_CORRECTION_US &&
           value.phaseCorrectionUs <= MAX_PHASE_CORRECTION_US &&
           (value.currentPolarity == 1 || value.currentPolarity == -1);
}

void loadCalibration()
{
    calibration = defaultCalibration();

    Preferences preferences;
    // L'ouverture en ecriture cree l'espace de noms au premier demarrage.
    // Une ouverture en lecture seule echouerait tant qu'il n'existe pas.
    if (!preferences.begin(NVS_NAMESPACE, false))
    {
        Serial.println("[AC CAL] NVS indisponible, valeurs par defaut utilisees.");
        return;
    }

    const bool storedVersionValid =
        preferences.getUChar("version", 0U) == CALIBRATION_VERSION;
    bool storedCalibrationValid = false;
    if (storedVersionValid)
    {
        Calibration stored = {};
        stored.voltageGain = preferences.getFloat(
            "v_gain", calibration.voltageGain
        );
        stored.currentGain = preferences.getFloat(
            "i_gain", calibration.currentGain
        );
        stored.noiseFloorA = preferences.getFloat(
            "noise_a", calibration.noiseFloorA
        );
        stored.phaseCorrectionUs = preferences.getInt(
            "phase_us", calibration.phaseCorrectionUs
        );
        stored.currentPolarity = preferences.getChar(
            "polarity", calibration.currentPolarity
        );

        if (calibrationIsPlausible(stored))
        {
            calibration = stored;
            storedCalibrationValid = true;
        }
        else
        {
            Serial.println("[AC CAL] Calibration NVS invalide, valeurs par defaut.");
        }
    }

    if (!storedCalibrationValid)
    {
        // Initialise une calibration complete et coherente dans une NVS neuve
        // ou apres la detection de donnees corrompues.
        preferences.putFloat("v_gain", calibration.voltageGain);
        preferences.putFloat("i_gain", calibration.currentGain);
        preferences.putFloat("noise_a", calibration.noiseFloorA);
        preferences.putInt("phase_us", calibration.phaseCorrectionUs);
        preferences.putChar("polarity", calibration.currentPolarity);
        preferences.putUChar("version", CALIBRATION_VERSION);
    }

    preferences.end();
}

bool saveCalibration()
{
    if (!calibrationIsPlausible(calibration))
    {
        return false;
    }

    Preferences preferences;
    if (!preferences.begin(NVS_NAMESPACE, false))
    {
        return false;
    }

    bool saved = true;
    saved &= preferences.putUChar("version", CALIBRATION_VERSION) == 1U;
    saved &= preferences.putFloat("v_gain", calibration.voltageGain) ==
             sizeof(float);
    saved &= preferences.putFloat("i_gain", calibration.currentGain) ==
             sizeof(float);
    saved &= preferences.putFloat("noise_a", calibration.noiseFloorA) ==
             sizeof(float);
    saved &= preferences.putInt("phase_us", calibration.phaseCorrectionUs) ==
             sizeof(int32_t);
    saved &= preferences.putChar("polarity", calibration.currentPolarity) ==
             sizeof(int8_t);
    preferences.end();
    return saved;
}

uint16_t readAdcMilliVolts(uint8_t pin, uint32_t& relativeTimeUs,
                           uint32_t acquisitionStartUs)
{
    const uint32_t before = micros();
    uint32_t value = analogReadMilliVolts(pin);
    const uint32_t after = micros();

    relativeTimeUs = (before - acquisitionStartUs) + ((after - before) / 2U);
    if (value > UINT16_MAX)
    {
        value = UINT16_MAX;
    }
    return static_cast<uint16_t>(value);
}

void acquireSamples()
{
    samples.count = 0U;
    samples.durationUs = 0U;
    samples.windowComplete = false;

    const uint32_t startedAtUs = micros();
    const uint32_t windowUs = AC_MEASUREMENT_WINDOW_MS * 1000UL;
    uint32_t nextSampleUs = startedAtUs;

    while (micros() - startedAtUs < windowUs &&
           samples.count < AC_MAXIMUM_SAMPLE_COUNT)
    {
        const uint32_t index = samples.count;
        samples.voltageMv[index] = readAdcMilliVolts(
            ZMPT101B_ADC_PIN,
            samples.voltageTimeUs[index],
            startedAtUs
        );
        samples.currentMv[index] = readAdcMilliVolts(
            ACS712_ADC_PIN,
            samples.currentTimeUs[index],
            startedAtUs
        );
        samples.count++;

        // Cadence absolue : environ 3333 paires/s avec 300 us. Elle garantit
        // que les 4096 cases couvrent toute la fenetre d'une seconde.
        nextSampleUs += AC_SAMPLE_PERIOD_US;
        const int32_t timeUntilNextSampleUs =
            static_cast<int32_t>(nextSampleUs - micros());
        if (timeUntilNextSampleUs > 0)
        {
            delayMicroseconds(static_cast<uint32_t>(timeUntilNextSampleUs));
        }
    }

    samples.durationUs = micros() - startedAtUs;
    samples.windowComplete = samples.durationUs >= windowUs;
}

bool interpolateVoltageMv(int32_t targetTimeUs, double voltageMeanMv,
                          float& centeredVoltageMv)
{
    if (samples.count < 2U || targetTimeUs < 0 ||
        static_cast<uint32_t>(targetTimeUs) < samples.voltageTimeUs[0] ||
        static_cast<uint32_t>(targetTimeUs) >
            samples.voltageTimeUs[samples.count - 1U])
    {
        return false;
    }

    uint32_t low = 0U;
    uint32_t high = samples.count - 1U;
    while (high - low > 1U)
    {
        const uint32_t middle = low + ((high - low) / 2U);
        if (samples.voltageTimeUs[middle] <=
            static_cast<uint32_t>(targetTimeUs))
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }

    const uint32_t t0 = samples.voltageTimeUs[low];
    const uint32_t t1 = samples.voltageTimeUs[high];
    if (t1 == t0)
    {
        return false;
    }

    const float fraction =
        static_cast<float>(static_cast<uint32_t>(targetTimeUs) - t0) /
        static_cast<float>(t1 - t0);
    const float interpolated =
        static_cast<float>(samples.voltageMv[low]) +
        fraction * (static_cast<float>(samples.voltageMv[high]) -
                    static_cast<float>(samples.voltageMv[low]));
    centeredVoltageMv = interpolated - static_cast<float>(voltageMeanMv);
    return true;
}

float activePowerForPhase(int32_t phaseCorrectionUs,
                          double voltageMeanMv, double currentMeanMv,
                          uint32_t& pairedSampleCount)
{
    double sumInstantaneousPower = 0.0;
    pairedSampleCount = 0U;

    for (uint32_t index = 0U; index < samples.count; ++index)
    {
        const int32_t targetVoltageTimeUs =
            static_cast<int32_t>(samples.currentTimeUs[index]) +
            phaseCorrectionUs;
        float centeredVoltageMv = 0.0F;
        if (!interpolateVoltageMv(
                targetVoltageTimeUs,
                voltageMeanMv,
                centeredVoltageMv
            ))
        {
            continue;
        }

        const float centeredCurrentMv =
            static_cast<float>(samples.currentMv[index]) -
            static_cast<float>(currentMeanMv);
        sumInstantaneousPower +=
            static_cast<double>(centeredVoltageMv) * calibration.voltageGain *
            static_cast<double>(centeredCurrentMv) * calibration.currentGain;
        pairedSampleCount++;
    }

    if (pairedSampleCount == 0U)
    {
        return 0.0F;
    }

    return static_cast<float>(
        sumInstantaneousPower / static_cast<double>(pairedSampleCount)
    );
}

ACPowerData analyzeSamples()
{
    ACPowerData result = {};
    result.sampleCount = samples.count;
    result.acquisitionDurationMs = samples.durationUs / 1000UL;
    result.windowComplete = samples.windowComplete;
    if (samples.count == 0U)
    {
        return result;
    }

    double voltageSum = 0.0;
    double currentSum = 0.0;
    uint16_t voltageMinimum = UINT16_MAX;
    uint16_t currentMinimum = UINT16_MAX;
    uint16_t voltageMaximum = 0U;
    uint16_t currentMaximum = 0U;

    for (uint32_t index = 0U; index < samples.count; ++index)
    {
        const uint16_t voltage = samples.voltageMv[index];
        const uint16_t current = samples.currentMv[index];
        voltageSum += voltage;
        currentSum += current;
        if (voltage < voltageMinimum) voltageMinimum = voltage;
        if (voltage > voltageMaximum) voltageMaximum = voltage;
        if (current < currentMinimum) currentMinimum = current;
        if (current > currentMaximum) currentMaximum = current;
    }

    const double voltageMean = voltageSum / samples.count;
    const double currentMean = currentSum / samples.count;
    double voltageSquareSum = 0.0;
    double currentSquareSum = 0.0;

    for (uint32_t index = 0U; index < samples.count; ++index)
    {
        const double voltage = samples.voltageMv[index] - voltageMean;
        const double current = samples.currentMv[index] - currentMean;
        voltageSquareSum += voltage * voltage;
        currentSquareSum += current * current;
    }

    result.voltageBiasMv = static_cast<float>(voltageMean);
    result.currentBiasMv = static_cast<float>(currentMean);
    result.voltageSignalRmsMv = static_cast<float>(
        sqrt(voltageSquareSum / samples.count)
    );
    result.currentSignalRmsMv = static_cast<float>(
        sqrt(currentSquareSum / samples.count)
    );
    result.voltageRms = result.voltageSignalRmsMv * calibration.voltageGain;
    result.currentRms = result.currentSignalRmsMv * calibration.currentGain;
    const float negativeCurrentPeakMv =
        static_cast<float>(currentMean) - currentMinimum;
    const float positiveCurrentPeakMv =
        currentMaximum - static_cast<float>(currentMean);
    result.currentPeak =
        fmaxf(negativeCurrentPeakMv, positiveCurrentPeakMv) *
        calibration.currentGain;

    if (result.voltageRms < ABSENT_VOLTAGE_THRESHOLD_V)
    {
        result.voltageRms = 0.0F;
    }
    if (result.currentRms < calibration.noiseFloorA)
    {
        result.currentRms = 0.0F;
    }

    result.apparentPower = result.voltageRms * result.currentRms;
    uint32_t pairedSampleCount = 0U;
    result.activePower = activePowerForPhase(
        calibration.phaseCorrectionUs,
        voltageMean,
        currentMean,
        pairedSampleCount
    ) * calibration.currentPolarity;

    if (result.currentRms == 0.0F || result.voltageRms == 0.0F)
    {
        result.activePower = 0.0F;
        result.powerFactor = 0.0F;
    }
    else
    {
        result.powerFactor = result.activePower / result.apparentPower;
        if (result.powerFactor > 1.0F) result.powerFactor = 1.0F;
        if (result.powerFactor < -1.0F) result.powerFactor = -1.0F;
    }

    result.voltageBiasValid =
        result.voltageBiasMv >= BIAS_MIN_MV &&
        result.voltageBiasMv <= BIAS_MAX_MV;
    result.currentBiasValid =
        result.currentBiasMv >= BIAS_MIN_MV &&
        result.currentBiasMv <= BIAS_MAX_MV;
    result.voltageClipped =
        voltageMinimum <= ADC_CLIP_LOW_MV ||
        voltageMaximum >= ADC_CLIP_HIGH_MV;
    result.currentClipped =
        currentMinimum <= ADC_CLIP_LOW_MV ||
        currentMaximum >= ADC_CLIP_HIGH_MV;
    // Le calibre +/-30 A porte sur la valeur instantanee. Le controle de
    // crete reste donc valable aussi pour une charge non sinusoidale.
    result.currentOverRange = result.currentPeak >= 30.0F;
    result.enoughSamples =
        samples.windowComplete &&
        samples.count >= AC_MINIMUM_SAMPLE_COUNT &&
        pairedSampleCount >= AC_MINIMUM_SAMPLE_COUNT;
    result.valid =
        result.enoughSamples &&
        result.voltageBiasValid &&
        result.currentBiasValid &&
        !result.voltageClipped &&
        !result.currentClipped &&
        !result.currentOverRange;
    return result;
}

void printMeasurementInternal(const ACPowerData& value)
{
    Serial.print("[AC] V=");
    Serial.print(value.voltageRms, 1);
    Serial.print(" V | I=");
    Serial.print(value.currentRms, 3);
    Serial.print(" A | P=");
    Serial.print(value.activePower, 1);
    Serial.print(" W | S=");
    Serial.print(value.apparentPower, 1);
    Serial.print(" VA | PF=");
    Serial.print(value.powerFactor, 3);
    Serial.print(" | echantillons=");
    Serial.print(value.sampleCount);
    Serial.print("/");
    Serial.print(value.acquisitionDurationMs);
    Serial.print(" ms");
    Serial.print(" | valide=");
    Serial.println(value.valid ? "oui" : "non");
}

bool parseFloatArgument(const String& line, const char* prefix, float& value)
{
    if (!line.startsWith(prefix))
    {
        return false;
    }
    const String argument = line.substring(strlen(prefix));
    char* end = nullptr;
    value = strtof(argument.c_str(), &end);
    while (end != nullptr && *end == ' ')
    {
        ++end;
    }
    return end != argument.c_str() && end != nullptr && *end == '\0' &&
           isfinite(value);
}

bool requireRecentUsableMeasurement(const char* operation)
{
    if (lastMeasurement.sampleCount < AC_MINIMUM_SAMPLE_COUNT)
    {
        Serial.print("[AC CAL] ");
        Serial.print(operation);
        Serial.println(" impossible : lancer d'abord AC MEASURE.");
        return false;
    }
    return true;
}

void applyAndSaveCalibration()
{
    lastMeasurement = analyzeSamples();
    Serial.println(
        saveCalibration()
            ? "[AC CAL] Calibration enregistree dans la NVS."
            : "[AC CAL] Echec de sauvegarde NVS."
    );
    printACPowerCalibration();
}

void autoCalibratePhase()
{
    Serial.println(
        "[AC CAL] Acquisition : utiliser uniquement une charge resistive stable."
    );
    acquireSamples();
    lastMeasurement = analyzeSamples();
    if (!lastMeasurement.voltageBiasValid ||
        !lastMeasurement.currentBiasValid ||
        lastMeasurement.voltageClipped ||
        lastMeasurement.currentClipped ||
        lastMeasurement.voltageRms < ABSENT_VOLTAGE_THRESHOLD_V ||
        lastMeasurement.currentRms < calibration.noiseFloorA)
    {
        Serial.println("[AC CAL] Signaux insuffisants ou invalides; phase conservee.");
        printMeasurementInternal(lastMeasurement);
        return;
    }

    double voltageSum = 0.0;
    double currentSum = 0.0;
    for (uint32_t index = 0U; index < samples.count; ++index)
    {
        voltageSum += samples.voltageMv[index];
        currentSum += samples.currentMv[index];
    }
    const double voltageMean = voltageSum / samples.count;
    const double currentMean = currentSum / samples.count;

    int32_t bestPhaseUs = 0;
    float bestPowerW = 0.0F;
    float bestMagnitudeW = -1.0F;
    for (int32_t phaseUs = -AUTO_PHASE_LIMIT_US;
         phaseUs <= AUTO_PHASE_LIMIT_US;
         phaseUs += AUTO_PHASE_STEP_US)
    {
        uint32_t pairedCount = 0U;
        const float powerW = activePowerForPhase(
            phaseUs,
            voltageMean,
            currentMean,
            pairedCount
        );
        const float magnitudeW = fabsf(powerW);
        if (pairedCount >= AC_MINIMUM_SAMPLE_COUNT &&
            magnitudeW > bestMagnitudeW)
        {
            bestMagnitudeW = magnitudeW;
            bestPowerW = powerW;
            bestPhaseUs = phaseUs;
        }
    }

    const float apparentPower =
        lastMeasurement.voltageRms * lastMeasurement.currentRms;
    const float bestPowerFactor =
        apparentPower > 0.0F ? bestMagnitudeW / apparentPower : 0.0F;
    if (bestMagnitudeW < 0.0F || bestPowerFactor < 0.50F)
    {
        Serial.println(
            "[AC CAL] Echec : PF trop faible pour identifier une charge resistive."
        );
        return;
    }

    calibration.phaseCorrectionUs = bestPhaseUs;
    calibration.currentPolarity = bestPowerW >= 0.0F ? 1 : -1;
    applyAndSaveCalibration();
    Serial.print("[AC CAL] PF obtenu sur la charge resistive : ");
    Serial.println(lastMeasurement.powerFactor, 3);
}

void processConsoleLine(String line)
{
    line.trim();
    line.toUpperCase();

    if (line == "AC CAL SHOW")
    {
        printACPowerCalibration();
        return;
    }
    if (line == "AC MEASURE")
    {
        lastMeasurement = readACPowerSensor();
        printMeasurementInternal(lastMeasurement);
        return;
    }
    if (line == "AC CAL PHASE_AUTO")
    {
        autoCalibratePhase();
        return;
    }
    if (line == "AC CAL ZERO")
    {
        Serial.println("[AC CAL] Acquisition du bruit : la charge doit etre coupee.");
        acquireSamples();
        lastMeasurement = analyzeSamples();
        if (!lastMeasurement.currentBiasValid ||
            lastMeasurement.currentClipped ||
            samples.count < AC_MINIMUM_SAMPLE_COUNT)
        {
            Serial.println("[AC CAL] Signal ACS712 invalide; seuil conserve.");
            return;
        }
        const float measuredNoiseA =
            lastMeasurement.currentSignalRmsMv * calibration.currentGain;
        calibration.noiseFloorA = fmaxf(0.02F, measuredNoiseA * 3.0F);
        if (calibration.noiseFloorA > 5.0F)
        {
            calibration.noiseFloorA = 5.0F;
        }
        applyAndSaveCalibration();
        return;
    }
    if (line == "AC CAL RESET")
    {
        calibration = defaultCalibration();
        applyAndSaveCalibration();
        return;
    }

    float argument = 0.0F;
    if (parseFloatArgument(line, "AC CAL VOLTAGE ", argument))
    {
        if (argument < 10.0F || argument > 300.0F)
        {
            Serial.println("[AC CAL] Reference tension invalide (10 a 300 V).");
            return;
        }
        if (!requireRecentUsableMeasurement("Etalonnage tension") ||
            !lastMeasurement.voltageBiasValid ||
            lastMeasurement.voltageClipped ||
            lastMeasurement.voltageSignalRmsMv <= 0.0F)
        {
            Serial.println("[AC CAL] Signal tension recent inutilisable.");
            return;
        }
        calibration.voltageGain =
            argument / lastMeasurement.voltageSignalRmsMv;
        applyAndSaveCalibration();
        return;
    }
    if (parseFloatArgument(line, "AC CAL CURRENT ", argument))
    {
        if (argument < 0.10F || argument > ACS712_MAX_SINE_RMS_A)
        {
            Serial.println("[AC CAL] Reference courant invalide (0,10 a 21,21 A RMS).");
            return;
        }
        if (!requireRecentUsableMeasurement("Etalonnage courant") ||
            !lastMeasurement.currentBiasValid ||
            lastMeasurement.currentClipped ||
            lastMeasurement.currentSignalRmsMv <= 0.0F)
        {
            Serial.println("[AC CAL] Signal courant recent inutilisable.");
            return;
        }
        calibration.currentGain =
            argument / lastMeasurement.currentSignalRmsMv;
        applyAndSaveCalibration();
        return;
    }
    if (parseFloatArgument(line, "AC CAL PHASE_US ", argument))
    {
        const int32_t phaseUs = static_cast<int32_t>(lroundf(argument));
        if (phaseUs < -MAX_PHASE_CORRECTION_US ||
            phaseUs > MAX_PHASE_CORRECTION_US)
        {
            Serial.println("[AC CAL] Phase invalide (-5000 a +5000 us).");
            return;
        }
        calibration.phaseCorrectionUs = phaseUs;
        applyAndSaveCalibration();
        return;
    }
    if (parseFloatArgument(line, "AC CAL POLARITY ", argument))
    {
        if (argument != 1.0F && argument != -1.0F)
        {
            Serial.println("[AC CAL] Polarite attendue : 1 ou -1.");
            return;
        }
        calibration.currentPolarity = argument > 0.0F ? 1 : -1;
        applyAndSaveCalibration();
        return;
    }

    if (line.startsWith("AC"))
    {
        Serial.println(
            "[AC CAL] Commande inconnue. Utiliser AC CAL SHOW pour l'aide."
        );
    }
}
} // namespace

bool initACPowerSensor()
{
#if AC_MEASUREMENT_ENABLED
    analogReadResolution(12);
    analogSetPinAttenuation(ZMPT101B_ADC_PIN, ADC_11db);
    analogSetPinAttenuation(ACS712_ADC_PIN, ADC_11db);
    (void)analogReadMilliVolts(ZMPT101B_ADC_PIN);
    (void)analogReadMilliVolts(ACS712_ADC_PIN);
    loadCalibration();
    sensorAvailable = true;
    printACPowerCalibration();
    return true;
#else
    sensorAvailable = false;
    return false;
#endif
}

ACPowerData readACPowerSensor()
{
    if (!sensorAvailable)
    {
        return ACPowerData{};
    }
    acquireSamples();
    lastMeasurement = analyzeSamples();
    return lastMeasurement;
}

bool isACPowerSensorAvailable()
{
    return sensorAvailable;
}

void printACPowerCalibration()
{
    Serial.println();
    Serial.println("[AC CAL] ZMPT101B + ACS712-30A");
    Serial.print("[AC CAL] gain tension V/mV ADC = ");
    Serial.println(calibration.voltageGain, 6);
    Serial.print("[AC CAL] gain courant A/mV ADC = ");
    Serial.println(calibration.currentGain, 6);
    Serial.print("[AC CAL] seuil bruit courant A = ");
    Serial.println(calibration.noiseFloorA, 3);
    Serial.print("[AC CAL] correction phase us = ");
    Serial.println(calibration.phaseCorrectionUs);
    Serial.print("[AC CAL] polarite courant = ");
    Serial.println(calibration.currentPolarity);
    Serial.println("[AC CAL] Commandes : AC MEASURE, AC CAL ZERO,");
    Serial.println("[AC CAL] AC CAL VOLTAGE <V>, AC CAL CURRENT <A>,");
    Serial.println("[AC CAL] AC CAL PHASE_AUTO, AC CAL PHASE_US <us>,");
    Serial.println("[AC CAL] AC CAL POLARITY <1|-1>, AC CAL RESET.");
}

void printACPowerMeasurement(const ACPowerData& value)
{
    printMeasurementInternal(value);

    Serial.print("[AC ADC] signal V/I=");
    Serial.print(value.voltageSignalRmsMv, 1);
    Serial.print('/');
    Serial.print(value.currentSignalRmsMv, 1);
    Serial.print(" mV RMS | bias V/I=");
    Serial.print(value.voltageBiasMv, 1);
    Serial.print('/');
    Serial.print(value.currentBiasMv, 1);
    Serial.print(" mV | I crete=");
    Serial.print(value.currentPeak, 2);
    Serial.println(" A");

    if (!value.enoughSamples)
        Serial.println("[AC ECHEC] Fenetre ou nombre d'echantillons insuffisant.");
    if (!value.voltageBiasValid)
        Serial.println("[AC ECHEC] Bias ZMPT101B invalide : verifier le cablage.");
    if (!value.currentBiasValid)
        Serial.println("[AC ECHEC] Bias ACS712 invalide : verifier le cablage.");
    if (value.voltageClipped)
        Serial.println("[AC ECHEC] Signal ZMPT101B ecrete.");
    if (value.currentClipped)
        Serial.println("[AC ECHEC] Signal ACS712 ecrete.");
    if (value.currentOverRange)
        Serial.println("[AC DANGER] Calibre crete 30 A depasse ou approche.");
}

void updateACPowerCalibrationConsole()
{
    while (Serial.available() > 0)
    {
        const char received = static_cast<char>(Serial.read());
        if (received == '\r')
        {
            continue;
        }
        if (received == '\n')
        {
            consoleLine[consoleLength] = '\0';
            processConsoleLine(String(consoleLine));
            consoleLength = 0U;
            continue;
        }
        if (consoleLength + 1U < sizeof(consoleLine))
        {
            consoleLine[consoleLength++] = received;
        }
        else
        {
            consoleLength = 0U;
            Serial.println("[AC CAL] Ligne trop longue, commande abandonnee.");
        }
    }
}
