#include <Arduino.h>

// Pour le materiel reel, definir BUZZER_ENABLED=1 et choisir BUZZER_PASSIVE
// et BUZZER_ACTIVE_HIGH dans config.h ou dans les options de compilation.
#include "../../../iot_box/src/config.h"
#include "../../../iot_box/src/pins.h"
#include "../../../iot_box/src/indicators/indicators.h"
#include "../../../iot_box/src/indicators/indicators.cpp"

namespace {
struct TestStep { const char* name; uint32_t durationMs; };
const TestStep steps[] = {
    {"VERT fixe (cablage)", 1000},
    {"VERT heartbeat : 3 cycles", 6000},
    {"BLEU connexion lente", 3000},
    {"BLEU transport en attente rapide", 3000},
    {"BLEU en ligne fixe", 2000},
    {"ROUGE warning lent", 3000},
    {"ROUGE critical rapide", 3000},
    {"ROUGE geofence rapide", 3000},
    {"BUZZER INFO", 2000},
    {"BUZZER WARNING", 2000},
    {"BUZZER CRITICAL", 2000},
    {"BUZZER GEOFENCE", 2500}
};
constexpr uint8_t stepCount = sizeof(steps) / sizeof(steps[0]);
uint8_t step = 0;
uint32_t stepSince = 0;

void enterStep() {
    stepSince = millis();
    setCommunicationIndicatorState(COMM_OFFLINE);
    setAlertIndicatorState(ALERT_NONE);
    if (step == stepCount) {
        Serial.println("[TEST] Termine. Heartbeat seul. Envoyer R pour recommencer.");
        return;
    }
    Serial.printf("[TEST] %u/%u %s\n", step + 1, stepCount, steps[step].name);
    switch (step) {
        case 1: setSystemIndicatorState(SYSTEM_RUNNING); break;
        case 2: setCommunicationIndicatorState(COMM_CONNECTING); break;
        case 3: setCommunicationIndicatorState(COMM_TRANSPORT_PENDING); break;
        case 4: setCommunicationIndicatorState(COMM_ONLINE); break;
        case 5: setAlertIndicatorState(ALERT_WARNING); break;
        case 6: setAlertIndicatorState(ALERT_CRITICAL); break;
        case 7: setAlertIndicatorState(ALERT_GEOFENCE); break;
        case 8: triggerBuzzer(BUZZER_INFO); break;
        case 9: triggerBuzzer(BUZZER_WARNING); break;
        case 10: triggerBuzzer(BUZZER_CRITICAL); break;
        case 11: triggerBuzzer(BUZZER_GEOFENCE); break;
        default: break;
    }
}
}

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    initIndicators();
    Serial.printf("[TEST] GPIO V/B/R/buzzer = %u/%u/%u/%u\n",
                  LED_SYSTEM_PIN, LED_COMM_PIN, LED_ALERT_PIN, BUZZER_PIN);
    Serial.printf("[TEST] BUZZER_ENABLED=%d PASSIVE=%d ACTIVE_HIGH=%d\n",
                  BUZZER_ENABLED, BUZZER_PASSIVE, BUZZER_ACTIVE_HIGH);
    enterStep();
}

void loop() {
    updateIndicators();
    if (Serial.available() && Serial.read() == 'R') {
        triggerBuzzer(BUZZER_NONE);
        step = 0;
        enterStep();
    }
    if (step < stepCount && uint32_t(millis() - stepSince) >= steps[step].durationMs) {
        ++step;
        enterStep();
    }
    // Exception de banc de test : fixe pour verifier le cablage initial.
    if (step == 0) digitalWrite(LED_SYSTEM_PIN, LED_ACTIVE_HIGH ? HIGH : LOW);
}
