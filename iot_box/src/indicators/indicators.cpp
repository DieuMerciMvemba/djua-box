#include <Arduino.h>
#include "../config.h"
#include "../pins.h"
#include "indicators.h"
#if BUZZER_ENABLED
#include <esp_timer.h>
#endif

namespace {
SystemIndicatorState systemState = SYSTEM_BOOTING;
CommunicationIndicatorState commState = COMM_OFFLINE;
AlertIndicatorState alertState = ALERT_NONE;
uint32_t systemSince = 0, commSince = 0, alertSince = 0;
const char* const systemNames[] = {"BOOTING", "RUNNING"};
const char* const commNames[] = {"OFFLINE", "CONNECTING", "TRANSPORT_PENDING", "ONLINE"};
const char* const alertNames[] = {"NONE", "WARNING", "CRITICAL", "GEOFENCE"};

static_assert(SYSTEM_BOOT_BLINK_MS > 0 && COMM_SLOW_BLINK_MS > 0 &&
              COMM_FAST_BLINK_MS > 0 && ALERT_WARNING_BLINK_MS > 0 &&
              ALERT_CRITICAL_BLINK_MS > 0, "Durees LED invalides");
static_assert(SYSTEM_HEARTBEAT_ON_MS > 0 &&
              SYSTEM_HEARTBEAT_ON_MS < SYSTEM_HEARTBEAT_PERIOD_MS,
              "Heartbeat invalide");

void led(uint8_t pin, bool on) {
    digitalWrite(pin, (on == bool(LED_ACTIVE_HIGH)) ? HIGH : LOW);
}

#if BUZZER_ENABLED
// Le timer ESP est une tache, pas une ISR. Il ne lit aucun capteur et
// ne pilote jamais le heartbeat : celui-ci depend uniquement de loop().
// Les operations LEDC restent hors de la section critique.
static_assert(BUZZER_SHORT_BEEP_MS > 0 && BUZZER_LONG_BEEP_MS > 0 &&
              BUZZER_GAP_MS > 0 && BUZZER_GEOFENCE_GAP_MS > 0 &&
              BUZZER_GEOFENCE_SHORT_MS > 0 && BUZZER_SERVICE_PERIOD_MS > 0 &&
              BUZZER_FREQUENCY_HZ > 0, "Configuration buzzer invalide");
esp_timer_handle_t buzzerTimer = nullptr;
portMUX_TYPE buzzerMux = portMUX_INITIALIZER_UNLOCKED;
bool buzzerReady = false;
BuzzerEvent requested = BUZZER_NONE, playing = BUZZER_NONE;
bool cancelRequested = false;
uint32_t buzzerSince = 0;
bool buzzerOutput = false;
const char* const buzzerNames[] = {"NONE", "INFO", "WARNING", "CRITICAL", "GEOFENCE"};

void buzzerWrite(bool on) {
    if (buzzerOutput == on) return;
    buzzerOutput = on;
#if BUZZER_PASSIVE
    // API par broche verifiee avec esp32:esp32 3.3.11.
    ledcWriteTone(BUZZER_PIN, on ? BUZZER_FREQUENCY_HZ : 0);
    if (!on) ledcWrite(BUZZER_PIN, BUZZER_ACTIVE_HIGH ? 0 : 255);
#else
    digitalWrite(BUZZER_PIN, (on == bool(BUZZER_ACTIVE_HIGH)) ? HIGH : LOW);
#endif
}

void serviceBuzzer(void*) {
    const uint32_t now = millis();
    portENTER_CRITICAL(&buzzerMux);
    if (cancelRequested) {
        playing = BUZZER_NONE;
        cancelRequested = false;
    }
    if (requested != BUZZER_NONE) {
        playing = requested;
        requested = BUZZER_NONE;
        buzzerSince = now;
    }
    const BuzzerEvent event = playing;
    portEXIT_CRITICAL(&buzzerMux);

    const uint32_t elapsed = now - buzzerSince;
    bool on = false, done = false;
    if (event == BUZZER_GEOFENCE) {
        const uint32_t second = BUZZER_LONG_BEEP_MS + BUZZER_GEOFENCE_GAP_MS;
        const uint32_t third = second + BUZZER_GEOFENCE_SHORT_MS + BUZZER_GAP_MS;
        on = elapsed < BUZZER_LONG_BEEP_MS ||
             (elapsed >= second && elapsed < second + BUZZER_GEOFENCE_SHORT_MS) ||
             (elapsed >= third && elapsed < third + BUZZER_GEOFENCE_SHORT_MS);
        done = elapsed >= third + BUZZER_GEOFENCE_SHORT_MS;
    } else if (event != BUZZER_NONE) {
        const uint32_t count = event == BUZZER_INFO ? 1 : event == BUZZER_WARNING ? 2 : 3;
        const uint32_t period = BUZZER_SHORT_BEEP_MS + BUZZER_GAP_MS;
        done = elapsed >= count * period - BUZZER_GAP_MS;
        on = !done && elapsed % period < BUZZER_SHORT_BEEP_MS;
    }
    buzzerWrite(on);
    if (done) {
        portENTER_CRITICAL(&buzzerMux);
        playing = BUZZER_NONE;
        portEXIT_CRITICAL(&buzzerMux);
    }
}
#endif
} // namespace

void initIndicators() {
    pinMode(LED_SYSTEM_PIN, OUTPUT);
    pinMode(LED_COMM_PIN, OUTPUT);
    pinMode(LED_ALERT_PIN, OUTPUT);
    systemState = SYSTEM_BOOTING;
    commState = COMM_OFFLINE;
    alertState = ALERT_NONE;
    systemSince = commSince = alertSince = millis();
#if BUZZER_ENABLED
    if (!buzzerReady) {
        pinMode(BUZZER_PIN, OUTPUT);
        digitalWrite(BUZZER_PIN, BUZZER_ACTIVE_HIGH ? LOW : HIGH);
#if BUZZER_PASSIVE
        if (!ledcAttach(BUZZER_PIN, BUZZER_FREQUENCY_HZ, 8)) {
            Serial.println("[BUZZER] Echec LEDC : sortie sonore desactivee");
            updateIndicators();
            return;
        }
        ledcWrite(BUZZER_PIN, BUZZER_ACTIVE_HIGH ? 0 : 255);
#endif
        esp_timer_create_args_t args = {};
        args.callback = serviceBuzzer;
        args.name = "djua_buzzer";
        if (esp_timer_create(&args, &buzzerTimer) == ESP_OK) {
            buzzerReady = esp_timer_start_periodic(
                buzzerTimer, BUZZER_SERVICE_PERIOD_MS * 1000ULL) == ESP_OK;
            if (!buzzerReady) {
                esp_timer_delete(buzzerTimer);
                buzzerTimer = nullptr;
            }
        }
        if (!buzzerReady) Serial.println("[BUZZER] Timer indisponible : sortie sonore desactivee");
    }
    triggerBuzzer(BUZZER_NONE);
#endif
    Serial.println("[INDICATORS] SYSTEM -> BOOTING");
    updateIndicators();
}

void updateIndicators() {
    const uint32_t now = millis();
    const uint32_t systemElapsed = now - systemSince;
    led(LED_SYSTEM_PIN, systemState == SYSTEM_BOOTING
        ? (systemElapsed / SYSTEM_BOOT_BLINK_MS) % 2 == 0
        : systemElapsed % SYSTEM_HEARTBEAT_PERIOD_MS < SYSTEM_HEARTBEAT_ON_MS);
    const uint32_t commInterval = commState == COMM_CONNECTING
        ? COMM_SLOW_BLINK_MS : COMM_FAST_BLINK_MS;
    led(LED_COMM_PIN, commState == COMM_ONLINE ||
        (commState != COMM_OFFLINE && ((now - commSince) / commInterval) % 2 == 0));
    const uint32_t alertInterval = alertState == ALERT_WARNING
        ? ALERT_WARNING_BLINK_MS : ALERT_CRITICAL_BLINK_MS;
    led(LED_ALERT_PIN, alertState != ALERT_NONE &&
        ((now - alertSince) / alertInterval) % 2 == 0);
}

void setSystemIndicatorState(SystemIndicatorState state) {
    if (state == systemState) return;
    systemState = state;
    systemSince = millis();
    Serial.printf("[INDICATORS] SYSTEM -> %s\n", systemNames[state]);
}
void setCommunicationIndicatorState(CommunicationIndicatorState state) {
    if (state == commState) return;
    commState = state;
    commSince = millis();
    Serial.printf("[INDICATORS] COMM -> %s\n", commNames[state]);
}
void setAlertIndicatorState(AlertIndicatorState state) {
    if (state == alertState) return;
    alertState = state;
    alertSince = millis();
    Serial.printf("[INDICATORS] ALERT -> %s\n", alertNames[state]);
}
void triggerBuzzer(BuzzerEvent event) {
#if BUZZER_ENABLED
    if (!buzzerReady) return;
    bool accepted = false;
    portENTER_CRITICAL(&buzzerMux);
    if (event == BUZZER_NONE) {
        requested = BUZZER_NONE;
        cancelRequested = true;
    } else if (event > requested && (cancelRequested || event > playing)) {
        requested = event;
        accepted = true;
    }
    portEXIT_CRITICAL(&buzzerMux);
    if (accepted) Serial.printf("[BUZZER] Pattern %s\n", buzzerNames[event]);
#else
    (void)event; // Aucune configuration ni ecriture de GPIO32 si desactive.
#endif
}
SystemIndicatorState getSystemIndicatorState() { return systemState; }
CommunicationIndicatorState getCommunicationIndicatorState() { return commState; }
AlertIndicatorState getAlertIndicatorState() { return alertState; }
