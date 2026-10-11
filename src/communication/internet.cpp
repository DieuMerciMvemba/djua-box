#include "internet.h"
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

    StaticJsonDocument<1280> doc;

    // Identifiant attendu par le backend
    doc["kit_id"] = DEVICE_ID;

    // GPS
    doc["latitude"] = data.latitude;
    doc["longitude"] = data.longitude;

    // =================================================
    // TIMESTAMP REEL DU DS1302
    // =================================================

    if (data.timestampValid)
    {
        doc["timestamp"] = data.timestamp;
        doc["timezone"] = RTC_TIMEZONE_LABEL;
    }

    doc["interval_seconds"] =
        data.intervalSeconds;

    // =================================================
    // SOLAR
    // =================================================

    JsonObject solar =
        doc.createNestedObject("solar");

    solar["voltage_v"] =
        data.solarVoltage;

    solar["current_a"] =
        data.solarCurrent;

    solar["power_w"] =
        data.solarPower;

    solar["energy_interval_wh"] =
        data.solarEnergyIntervalWh;
    if (!data.solarMeasurementValid)
    {
        solar["voltage_v"] = nullptr;
        solar["current_a"] = nullptr;
        solar["power_w"] = nullptr;
        solar["energy_interval_wh"] = nullptr;
    }

    // =================================================
    // BATTERY - VALEURS REELLES INA219
    // =================================================

    JsonObject battery =
        doc.createNestedObject("battery");

    battery["voltage_v"] =
        data.batteryVoltage;

    battery["current_a"] =
        data.batteryCurrent;

    battery["power_w"] =
        data.batteryPower;

    // =================================================
    // AC LOAD
    // =================================================

    JsonObject acLoad =
        doc.createNestedObject("ac_load");

    acLoad["voltage_v"] =
        data.acLoadVoltage;

    acLoad["current_a"] =
        data.acLoadCurrent;

    acLoad["active_power_w"] =
        data.acActivePower;

    acLoad["apparent_power_va"] =
        data.acApparentPower;

    acLoad["energy_interval_wh"] =
        data.acEnergyIntervalWh;

    // =================================================
    JsonObject dcLoad = doc.createNestedObject("dc_load");
    dcLoad["voltage_v"] = data.dcLoadVoltage;
    dcLoad["current_a"] = data.dcLoadCurrent;
    dcLoad["power_w"] = data.dcLoadPower;
    dcLoad["energy_interval_wh"] = data.dcLoadEnergyIntervalWh;
    if (!data.dcLoadMeasurementValid) {
        dcLoad["voltage_v"] = nullptr; dcLoad["current_a"] = nullptr; dcLoad["power_w"] = nullptr;
    }
    if (!data.dcLoadEnergyComplete) dcLoad["energy_interval_wh"] = nullptr;

    // CONVERSION JSON -> STRING
    // =================================================

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
