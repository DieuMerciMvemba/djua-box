#include <Arduino.h>
#include <math.h>

// ==============================================================
// TEST AUTONOME ESP32 : TENSION RMS ONDULEUR AVEC MODULE ZMPT101B
// ==============================================================
// Ce sketch est prevu pour le module complet avec ZMPT101B, LM358,
// potentiometre et broches VCC/GND/OUT.
// Il n'utilise aucun fichier du firmware principal.
//
// DANGER : le bornier AC du module est relie a une tension mortelle.
// Placer le module dans un boitier isolant. Couper l'onduleur avant toute
// intervention et faire valider le montage par une personne qualifiee.
//
// CABLAGE BASSE TENSION RECOMMANDE :
//
//   Module VCC -> 5V ESP32
//   Module GND -> GND ESP32
//   Module OUT -> 10 kohms -> GPIO34
//   GPIO34     -> 10 kohms -> GND ESP32
//
// Le diviseur 10k/10k reduit de moitie la sortie 0-5 V du module et
// protege l'entree 3,3 V de l'ESP32. Ne jamais relier OUT directement
// au GPIO lorsque le module est alimente en 5 V.
//
// CABLAGE HAUTE TENSION : les deux bornes AC du module se placent en
// parallele sur L et N a la sortie de l'onduleur. Ne jamais relier L ou N
// au GND de l'ESP32. Ne jamais manipuler le potentiometre sous tension.

constexpr uint32_t SERIAL_BAUD_RATE = 115200UL;
constexpr uint8_t ZMPT_ADC_PIN = 34; // ADC1, entree uniquement, libre dans DJUA

constexpr float OUTPUT_DIVIDER_TOP_OHMS = 10000.0F;    // OUT -> GPIO34
constexpr float OUTPUT_DIVIDER_BOTTOM_OHMS = 10000.0F; // GPIO34 -> GND
constexpr float OUTPUT_DIVIDER_RATIO =
    OUTPUT_DIVIDER_BOTTOM_OHMS /
    (OUTPUT_DIVIDER_TOP_OHMS + OUTPUT_DIVIDER_BOTTOM_OHMS);

// Calibration initiale simple : avec une tension connue, regler d'abord le
// potentiometre (hors tension entre chaque ajustement) pour obtenir :
// signal_ADC en mV RMS = tension du multimetre TRUE-RMS en volts.
// Exemple : 230,0 V au multimetre -> environ 230,0 mV RMS dans le moniteur.
// Pour une correction logicielle fine :
// nouveau facteur = ancien facteur * tension_multimetre / tension_affichee.
constexpr float CALIBRATION_VOLTS_PER_ADC_MV = 1.0000F;

constexpr uint32_t MEASUREMENT_WINDOW_MS = 1000UL;
constexpr uint32_t REPORT_INTERVAL_MS = 2000UL;
constexpr uint32_t SAMPLE_PAUSE_US = 100U;

constexpr float EXPECTED_MIN_V = 180.0F;
constexpr float EXPECTED_MAX_V = 260.0F;
constexpr float ABSENT_VOLTAGE_THRESHOLD_V = 5.0F;

// Plage utile prudente pour l'ESP32 classique avec attenuation 11 dB.
constexpr uint32_t ADC_CLIP_LOW_MV = 200U;
constexpr uint32_t ADC_CLIP_HIGH_MV = 2400U;
constexpr float BIAS_MIN_MV = 700.0F;
constexpr float BIAS_MAX_MV = 1800.0F;
constexpr uint32_t MINIMUM_SAMPLE_COUNT = 500U;

static_assert(OUTPUT_DIVIDER_RATIO > 0.0F && OUTPUT_DIVIDER_RATIO < 1.0F,
              "Le rapport du diviseur OUT doit etre compris entre 0 et 1.");
static_assert(CALIBRATION_VOLTS_PER_ADC_MV > 0.0F,
              "Le facteur de calibration doit etre positif.");

struct VoltageMeasurement
{
    float inverterRmsV;
    float adcSignalRmsMv;
    float adcBiasMv;
    uint32_t adcMinMv;
    uint32_t adcMaxMv;
    uint32_t sampleCount;
    bool biasValid;
    bool clipped;
    bool enoughSamples;
};

static VoltageMeasurement measureVoltageRms()
{
    // Algorithme de Welford : mesure simultanee de la composante continue
    // (BIAS) et de la valeur RMS de la composante alternative.
    uint32_t sampleCount = 0U;
    double meanMv = 0.0;
    double sumSquaredDifferences = 0.0;
    uint32_t minimumMv = UINT32_MAX;
    uint32_t maximumMv = 0U;

    const uint32_t startedAt = millis();

    while (millis() - startedAt < MEASUREMENT_WINDOW_MS)
    {
        const uint32_t sampleMv = analogReadMilliVolts(ZMPT_ADC_PIN);
        sampleCount++;

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

    VoltageMeasurement result = {};
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

    result.inverterRmsV =
        result.adcSignalRmsMv * CALIBRATION_VOLTS_PER_ADC_MV;

    // Evite d'afficher quelques volts fictifs lorsque l'onduleur est arrete.
    if (result.inverterRmsV < ABSENT_VOLTAGE_THRESHOLD_V)
    {
        result.inverterRmsV = 0.0F;
    }

    result.biasValid =
        result.adcBiasMv >= BIAS_MIN_MV &&
        result.adcBiasMv <= BIAS_MAX_MV;
    result.clipped =
        minimumMv <= ADC_CLIP_LOW_MV ||
        maximumMv >= ADC_CLIP_HIGH_MV;
    result.enoughSamples = sampleCount >= MINIMUM_SAMPLE_COUNT;

    return result;
}

static void printConfiguration()
{
    Serial.println();
    Serial.println("=== TEST ZMPT101B / TENSION ONDULEUR ===");
    Serial.println("Capteur : module ZMPT101B + LM358 + potentiometre");
    Serial.print("Entree ADC ESP32 : GPIO ");
    Serial.println(ZMPT_ADC_PIN);
    Serial.println("Alimentation module : 5 V");
    Serial.print("Diviseur OUT vers ADC : x");
    Serial.println(OUTPUT_DIVIDER_RATIO, 3);
    Serial.print("Calibration V/mV ADC : ");
    Serial.println(CALIBRATION_VOLTS_PER_ADC_MV, 4);
    Serial.println("ATTENTION : OUT passe obligatoirement par le diviseur 10k/10k.");
    Serial.println("ATTENTION : aucune borne AC ne va au GND ESP32.");
    Serial.println();
}

static void printMeasurement(const VoltageMeasurement& measurement)
{
    Serial.print("[MESURE] onduleur=");
    Serial.print(measurement.inverterRmsV, 1);
    Serial.print(" V RMS | signal_ADC=");
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
            "[ECHEC] Signal ADC ecrete : couper l'onduleur et verifier le montage."
        );
    }
    else if (measurement.inverterRmsV == 0.0F)
    {
        Serial.println("[INFO] Tension absente ou inferieure au seuil de 5 V.");
    }
    else if (
        measurement.inverterRmsV < EXPECTED_MIN_V ||
        measurement.inverterRmsV > EXPECTED_MAX_V
    )
    {
        Serial.println("[ALERTE] Tension RMS hors de la plage 180-260 V.");
    }
    else
    {
        Serial.println("[OK] Tension RMS dans la plage attendue.");
    }

    Serial.println();
}

void setup()
{
    Serial.begin(SERIAL_BAUD_RATE);
    delay(1000);

    analogReadResolution(12);
    analogSetPinAttenuation(ZMPT_ADC_PIN, ADC_11db);

    // Premiere conversion ignoree pour stabiliser la voie ADC.
    (void)analogReadMilliVolts(ZMPT_ADC_PIN);
    delay(100);

    printConfiguration();
}

void loop()
{
    const VoltageMeasurement measurement = measureVoltageRms();
    printMeasurement(measurement);

    // La fenetre de mesure dure deja 1 s : on attend encore 1 s pour
    // obtenir un nouveau rapport environ toutes les 2 secondes.
    delay(REPORT_INTERVAL_MS - MEASUREMENT_WINDOW_MS);
}
