#include <Arduino.h>
#include <math.h>

// ==============================================================
// TEST AUTONOME ESP32 : COURANT ALTERNATIF RMS AVEC ACS712
// ==============================================================
// Ce sketch mesure uniquement la composante AC du courant. Il retire
// automatiquement le point milieu (bias) de la sortie de l'ACS712.
//
// DANGER : les bornes de puissance de l'ACS712 peuvent etre reliees au
// secteur. Couper et consigner toute alimentation avant de cabler. Utiliser
// un module, des conducteurs, un boitier et des protections adaptes au
// courant et a la tension. Faire valider le montage par une personne qualifiee.
//
// CABLAGE BASSE TENSION :
//
//   ACS712 VCC -> 5V ESP32
//   ACS712 GND -> GND ESP32
//   ACS712 OUT -> 10 kohms -> GPIO35
//   GPIO35    -> 10 kohms -> GND ESP32
//
// Le diviseur 10k/10k reduit la sortie 0-5 V de moitie. Ne jamais relier
// OUT directement au GPIO35 lorsque l'ACS712 est alimente en 5 V.
//
// CABLAGE DU COURANT AC (en serie, jamais en parallele) :
//
//   Source phase -> IP+ ACS712
//   IP- ACS712   -> phase de la charge
//   Source neutre -> neutre de la charge
//
// Ne jamais relier IP+ ou IP- au GND de l'ESP32.

constexpr uint32_t SERIAL_BAUD_RATE = 115200UL;
constexpr uint8_t ACS712_ADC_PIN = 35; // ADC1, entree uniquement

constexpr float OUTPUT_DIVIDER_TOP_OHMS = 10000.0F;    // OUT -> GPIO35
constexpr float OUTPUT_DIVIDER_BOTTOM_OHMS = 10000.0F; // GPIO35 -> GND
constexpr float OUTPUT_DIVIDER_RATIO =
    OUTPUT_DIVIDER_BOTTOM_OHMS /
    (OUTPUT_DIVIDER_TOP_OHMS + OUTPUT_DIVIDER_BOTTOM_OHMS);

// Selectionner les deux valeurs correspondant au modele utilise :
//   ACS712-05B : +/-5 A crete,  185 mV/A
//   ACS712-20A : +/-20 A crete, 100 mV/A (configuration par defaut)
//   ACS712-30A : +/-30 A crete,  66 mV/A
constexpr float ACS712_FULL_SCALE_PEAK_A = 30.0F;
constexpr float ACS712_SENSITIVITY_MV_PER_A = 66.0F;
constexpr float ACS712_MAX_SINE_RMS_A =
    ACS712_FULL_SCALE_PEAK_A / 1.41421356237F;

// Apres comparaison avec une pince amperemetrique TRUE-RMS :
// nouveau facteur = ancien facteur * courant_reference / courant_affiche.
constexpr float CURRENT_CALIBRATION_FACTOR = 1.0000F;

constexpr uint32_t MEASUREMENT_WINDOW_MS = 1000UL;
constexpr uint32_t REPORT_INTERVAL_MS = 2000UL;
constexpr uint32_t SAMPLE_PAUSE_US = 100U;
constexpr uint32_t MINIMUM_SAMPLE_COUNT = 500U;

// Les petites valeurs dues au bruit de l'ADC sont ramenees a zero.
// Ajuster ce seuil apres avoir observe la mesure sans aucune charge.
constexpr float ZERO_CURRENT_THRESHOLD_A = 0.15F;

// Plage prudente apres le diviseur 10k/10k.
constexpr uint32_t ADC_CLIP_LOW_MV = 100U;
constexpr uint32_t ADC_CLIP_HIGH_MV = 2400U;
constexpr float BIAS_MIN_MV = 700.0F;
constexpr float BIAS_MAX_MV = 1800.0F;

static_assert(OUTPUT_DIVIDER_RATIO > 0.0F && OUTPUT_DIVIDER_RATIO < 1.0F,
              "Le rapport du diviseur OUT doit etre compris entre 0 et 1.");
static_assert(ACS712_FULL_SCALE_PEAK_A > 0.0F,
              "Le courant nominal ACS712 doit etre positif.");
static_assert(ACS712_SENSITIVITY_MV_PER_A > 0.0F,
              "La sensibilite ACS712 doit etre positive.");
static_assert(CURRENT_CALIBRATION_FACTOR > 0.0F,
              "Le facteur de calibration doit etre positif.");

struct CurrentMeasurement
{
    float currentRmsA;
    float adcSignalRmsMv;
    float adcBiasMv;
    uint32_t adcMinMv;
    uint32_t adcMaxMv;
    uint32_t sampleCount;
    bool biasValid;
    bool clipped;
    bool enoughSamples;
    bool overRange;
};

static CurrentMeasurement measureCurrentRms()
{
    uint32_t sampleCount = 0U;
    double meanMv = 0.0;
    double sumSquaredDifferences = 0.0;
    uint32_t minimumMv = UINT32_MAX;
    uint32_t maximumMv = 0U;

    const uint32_t startedAt = millis();
    while (millis() - startedAt < MEASUREMENT_WINDOW_MS)
    {
        const uint32_t sampleMv = analogReadMilliVolts(ACS712_ADC_PIN);
        sampleCount++;

        // Welford : calcul stable du bias et du RMS autour de ce bias.
        const double difference = static_cast<double>(sampleMv) - meanMv;
        meanMv += difference / static_cast<double>(sampleCount);
        const double differenceAfterMean =
            static_cast<double>(sampleMv) - meanMv;
        sumSquaredDifferences += difference * differenceAfterMean;

        if (sampleMv < minimumMv)
        {
            minimumMv = sampleMv;
        }
        if (sampleMv > maximumMv)
        {
            maximumMv = sampleMv;
        }

        delayMicroseconds(SAMPLE_PAUSE_US);
    }

    CurrentMeasurement result = {};
    result.sampleCount = sampleCount;
    result.adcMinMv = minimumMv;
    result.adcMaxMv = maximumMv;
    result.adcBiasMv = static_cast<float>(meanMv);

    if (sampleCount > 0U)
    {
        result.adcSignalRmsMv = static_cast<float>(
            sqrt(sumSquaredDifferences / static_cast<double>(sampleCount))
        );
    }

    const float adcSensitivityMvPerA =
        ACS712_SENSITIVITY_MV_PER_A * OUTPUT_DIVIDER_RATIO;
    result.currentRmsA =
        (result.adcSignalRmsMv / adcSensitivityMvPerA) *
        CURRENT_CALIBRATION_FACTOR;

    if (result.currentRmsA < ZERO_CURRENT_THRESHOLD_A)
    {
        result.currentRmsA = 0.0F;
    }

    result.biasValid =
        result.adcBiasMv >= BIAS_MIN_MV &&
        result.adcBiasMv <= BIAS_MAX_MV;
    result.clipped =
        minimumMv <= ADC_CLIP_LOW_MV ||
        maximumMv >= ADC_CLIP_HIGH_MV;
    result.enoughSamples = sampleCount >= MINIMUM_SAMPLE_COUNT;
    result.overRange = result.currentRmsA > ACS712_MAX_SINE_RMS_A;

    return result;
}

static void printConfiguration()
{
    Serial.println();
    Serial.println("=== TEST ACS712 / COURANT AC RMS ===");
    Serial.print("Entree ADC ESP32 : GPIO ");
    Serial.println(ACS712_ADC_PIN);
    Serial.println("Alimentation ACS712 : 5 V");
    Serial.print("Diviseur OUT vers ADC : x");
    Serial.println(OUTPUT_DIVIDER_RATIO, 3);
    Serial.print("Calibre configure : +/-");
    Serial.print(ACS712_FULL_SCALE_PEAK_A, 0);
    Serial.println(" A crete");
    Serial.print("Maximum sinusoidal correspondant : ");
    Serial.print(ACS712_MAX_SINE_RMS_A, 2);
    Serial.println(" A RMS");
    Serial.print("Sensibilite configuree : ");
    Serial.print(ACS712_SENSITIVITY_MV_PER_A, 1);
    Serial.println(" mV/A avant diviseur");
    Serial.print("Facteur de calibration : ");
    Serial.println(CURRENT_CALIBRATION_FACTOR, 4);
    Serial.println("ATTENTION : OUT passe obligatoirement par le diviseur 10k/10k.");
    Serial.println("ATTENTION : IP+ et IP- sont en serie avec la charge AC.");
    Serial.println();
}

static void printMeasurement(const CurrentMeasurement& measurement)
{
    Serial.print("[MESURE] courant=");
    Serial.print(measurement.currentRmsA, 3);
    Serial.print(" A RMS | signal_ADC=");
    Serial.print(measurement.adcSignalRmsMv, 1);
    Serial.print(" mV RMS | bias=");
    Serial.print(measurement.adcBiasMv, 1);
    Serial.print(" mV | min/max=");
    Serial.print(measurement.adcMinMv);
    Serial.print('/');
    Serial.print(measurement.adcMaxMv);
    Serial.print(" mV | echantillons=");
    Serial.println(measurement.sampleCount);

    if (!measurement.enoughSamples)
    {
        Serial.println("[ECHEC] Echantillonnage trop lent ou incomplet.");
    }
    else if (!measurement.biasValid)
    {
        Serial.println(
            "[ECHEC] BIAS incorrect : verifier VCC, GND, OUT et le diviseur."
        );
    }
    else if (measurement.clipped)
    {
        Serial.println(
            "[ECHEC] Signal ADC ecrete : couper la charge et verifier le montage."
        );
    }
    else if (measurement.overRange)
    {
        Serial.println("[DANGER] Courant crete susceptible de depasser le calibre.");
    }
    else if (measurement.currentRmsA == 0.0F)
    {
        Serial.println("[INFO] Courant absent ou inferieur au seuil de bruit.");
    }
    else
    {
        Serial.println("[OK] Mesure AC RMS valide.");
    }

    Serial.println();
}

void setup()
{
    Serial.begin(SERIAL_BAUD_RATE);
    delay(1000);

    analogReadResolution(12);
    analogSetPinAttenuation(ACS712_ADC_PIN, ADC_11db);

    (void)analogReadMilliVolts(ACS712_ADC_PIN);
    delay(100);

    printConfiguration();
}

void loop()
{
    const CurrentMeasurement measurement = measureCurrentRms();
    printMeasurement(measurement);

    delay(REPORT_INTERVAL_MS - MEASUREMENT_WINDOW_MS);
}
