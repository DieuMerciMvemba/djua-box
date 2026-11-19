#include "internet.h"
#include "../telemetry/json_v3.h"
#include "../config.h"
#if DJUA_COMM_PROFILE != DJUA_PROFILE_SMS_PRODUCTION

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "../config.h"

// =====================================================
// IOT BOX - COMMUNICATION BACKEND
// =====================================================

static unsigned long lastWiFiRetry = 0;
static unsigned long wifiAttemptStarted = 0;
static bool wifiConnecting = false;


// =====================================================
// CONNEXION WIFI
// =====================================================

void initInternet()
{
    Serial.println();
    Serial.print("[WiFi] Connexion a ");
    Serial.println(WIFI_SSID);

    WiFi.mode(WIFI_STA);

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );

    wifiAttemptStarted = millis();
    wifiConnecting = true;
}


// =====================================================
// RECONNEXION
// =====================================================

void updateInternet()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        if (wifiConnecting)
        {
            Serial.println("[WiFi] CONNECTE");
            Serial.print("[WiFi] IP : ");
            Serial.println(WiFi.localIP());
        }
        wifiConnecting = false;
        return;
    }

    unsigned long now = millis();

    if (wifiConnecting)
    {
        if (now - wifiAttemptStarted < WIFI_CONNECT_TIMEOUT_MS) return;
        wifiConnecting = false;
        lastWiFiRetry = now;
        WiFi.disconnect();
        Serial.println("[WiFi] CONNEXION IMPOSSIBLE");
        return;
    }

    if (now - lastWiFiRetry >= WIFI_RETRY_INTERVAL_MS)
    {
        lastWiFiRetry = now;

        Serial.println("[WiFi] Tentative de reconnexion...");

        WiFi.disconnect();
        WiFi.begin(
            WIFI_SSID,
            WIFI_PASSWORD
        );
        wifiAttemptStarted = now;
        wifiConnecting = true;
    }
}


// =====================================================
// ETAT CONNEXION
// =====================================================

bool isInternetConnected()
{
    return WiFi.status() == WL_CONNECTED;
}

bool isInternetConnecting()
{
    return wifiConnecting && !isInternetConnected();
}


// =====================================================
// ENVOI AU BACKEND
// =====================================================

#if ENABLE_HTTP_BACKEND
bool sendTelemetryToBackend(const TelemetryData& data)
{
    if (!isInternetConnected())
    {
        Serial.println(
            "[HTTP] Impossible : WiFi non connecte."
        );

        return false;
    }

    // =================================================
    // CONSTRUCTION DU JSON
    // =================================================

    JsonDocument doc;
    telemetryJsonV3(doc, data);
    if (doc.overflowed()) { Serial.println("[HTTP] Capacite JSON depassee"); return false; }
    String payload;

    serializeJson(
        doc,
        payload
    );

    Serial.println();
    Serial.println("========== TELEMETRIE ==========");
    Serial.println(payload);
    Serial.println("================================");

    // =================================================
    // HTTP POST
    // =================================================

    WiFiClient client;
    HTTPClient http;

    if (!http.begin(client, BACKEND_URL))
    {
        Serial.println(
            "[HTTP] Impossible d'ouvrir le endpoint."
        );

        return false;
    }

    http.setTimeout(10000);

    http.addHeader(
        "Content-Type",
        "application/json"
    );

    http.addHeader(
        "x-device-token",
        IOT_API_KEY
    );

    Serial.println("[HTTP] POST vers backend...");

    int httpCode =
        http.POST(payload);

    if (httpCode <= 0)
    {
        Serial.print("[HTTP] Erreur : ");
        Serial.println(
            http.errorToString(httpCode)
        );

        http.end();

        return false;
    }

    Serial.print("[HTTP] Code : ");
    Serial.println(httpCode);

    String response =
        http.getString();

    Serial.print("[HTTP] Reponse : ");
    Serial.println(response);

    bool success =
        httpCode >= 200 &&
        httpCode < 300;

    http.end();

    return success;
}
#else
bool sendTelemetryToBackend(const TelemetryData&) { return false; }
#endif
#else
// Verrou de transport : aucun appel externe ne peut activer le Wi-Fi/HTTP ici.
void initInternet() {}
void updateInternet() {}
bool isInternetConnected() { return false; }
bool isInternetConnecting() { return false; }
bool sendTelemetryToBackend(const TelemetryData&) { return false; }
#endif
