#include "mqtt.h"
#include "../telemetry/json_v3.h"
#include "../config.h"
#if DJUA_COMM_PROFILE == DJUA_PROFILE_MQTT_TEST

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#include "../config.h"

static WiFiClient mqttWiFiClient;
static PubSubClient mqttClient(mqttWiFiClient);
static unsigned long lastMQTTRetry = 0;
static char pendingGeofenceEventPayload[768] = {};
static bool hasPendingGeofenceEvent = false;

static String telemetryTopic()
{
    return String(MQTT_TOPIC_PREFIX) + "/" + DEVICE_ID + "/telemetry";
}

static String statusTopic()
{
    return String(MQTT_TOPIC_PREFIX) + "/" + DEVICE_ID + "/status";
}

static String geofenceTopic()
{
    return String(MQTT_TOPIC_PREFIX) + "/" + DEVICE_ID + "/geofence";
}

static String geofenceEventsTopic()
{
    return geofenceTopic() + "/events";
}

static bool publishPendingGeofenceEvent()
{
    if (!hasPendingGeofenceEvent || !mqttClient.connected())
    {
        return !hasPendingGeofenceEvent;
    }

    const String topic = geofenceEventsTopic();
    const bool sent = mqttClient.publish(
        topic.c_str(),
        pendingGeofenceEventPayload,
        false
    );

    if (sent)
    {
        hasPendingGeofenceEvent = false;
        pendingGeofenceEventPayload[0] = '\0';
        Serial.println("[MQTT] EVENEMENT GEOFENCE EN ATTENTE ENVOYE");
    }

    return sent;
}
static bool connectMQTT()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        return false;
    }

    Serial.print("[MQTT] Connexion a ");
    Serial.println(MQTT_BROKER_HOST);

    String clientId = String("djua-")  + DEVICE_ID;
    String willTopic = statusTopic();

    if (!mqttClient.connect(clientId.c_str(), willTopic.c_str(), 0, true, "offline"))
    {
        Serial.print("[MQTT] Echec, code : ");
        Serial.println(mqttClient.state());
        return false;
    }

    mqttClient.publish(willTopic.c_str(), "online", true);
    Serial.println("[MQTT] CONNECTE");
    Serial.print("[MQTT] Topic telemetry : ");
    Serial.println(telemetryTopic());
    Serial.print("[MQTT] Topic geofence : ");
    Serial.println(geofenceTopic());
    publishPendingGeofenceEvent();
    return true;
}

void initMQTT()
{
    mqttClient.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
    // The default PubSubClient packet limit is too small for the full payload.
    mqttClient.setBufferSize(4096);
}

void updateMQTT()
{
    if (!mqttClient.connected())
    {
        unsigned long now = millis();
        if (now - lastMQTTRetry >= MQTT_RETRY_INTERVAL_MS)
        {
            lastMQTTRetry = now;
            connectMQTT();
        }
        return;
    }

    mqttClient.loop();
    publishPendingGeofenceEvent();
}

bool isMQTTConnected()
{
    return mqttClient.connected();
}

bool sendTelemetryToMQTT(const TelemetryData& data)
{
    if (!mqttClient.connected())
    {
        Serial.println("[MQTT] Impossible : non connecte.");
        return false;
    }

    JsonDocument doc;
    telemetryJsonV3(doc, data);
    if (doc.overflowed()) { Serial.println("[MQTT] Capacite JSON depassee"); return false; }
    char payload[3072];
    size_t payloadSize = serializeJson(doc, payload, sizeof(payload));
    if (payloadSize == 0 || payloadSize >= sizeof(payload))
    {
        Serial.println("[MQTT] Erreur de construction JSON.");
        return false;
    }

    String topic = telemetryTopic();
    bool sent = mqttClient.publish(topic.c_str(), payload, false);
    Serial.println(sent ? "[MQTT] TELEMETRIE ENVOYEE" : "[MQTT] ECHEC ENVOI");
    return sent;
}

bool sendGeofenceToMQTT(
    const GeofenceResult& result,
    const char* timestamp,
    bool timestampValid
)
{
    StaticJsonDocument<768> doc;
    doc["kit_id"] = result.deviceId;

    if (timestampValid && timestamp != nullptr && timestamp[0] != '\0')
    {
        doc["timestamp"] = timestamp;
        doc["timezone"] = RTC_TIMEZONE_LABEL;
    }

    doc["state"] = geofenceStateToString(result.state);
    doc["event"] = geofenceEventToString(result.eventType);
    doc["position_usable"] = result.positionUsable;

    if (result.positionUsable)
    {
        doc["latitude"] = result.latitude;
        doc["longitude"] = result.longitude;
        doc["distance_m"] = result.distanceMeters;
    }
    else
    {
        doc["latitude"] = nullptr;
        doc["longitude"] = nullptr;
        doc["distance_m"] = nullptr;
    }

    JsonObject zone = doc.createNestedObject("zone");
    zone["center_latitude"] = GEOFENCE_CENTER_LAT;
    zone["center_longitude"] = GEOFENCE_CENTER_LON;
    zone["enter_radius_m"] = GEOFENCE_ENTER_RADIUS_M;
    zone["exit_radius_m"] = GEOFENCE_EXIT_RADIUS_M;

    JsonObject confirmation = doc.createNestedObject("confirmation");
    confirmation["candidate_state"] =
        geofenceStateToString(result.candidateState);
    confirmation["count"] = result.confirmationCount;
    confirmation["required"] = GEOFENCE_CONFIRM_COUNT;

    char payload[768];
    const size_t payloadSize = serializeJson(doc, payload, sizeof(payload));
    if (payloadSize == 0 || payloadSize >= sizeof(payload))
    {
        Serial.println("[MQTT] Erreur de construction JSON geofence.");
        return false;
    }

    const bool isEvent = result.eventType != GEOFENCE_EVENT_NONE;

    if (!mqttClient.connected())
    {
        if (isEvent)
        {
            strncpy(
                pendingGeofenceEventPayload,
                payload,
                sizeof(pendingGeofenceEventPayload) - 1
            );
            pendingGeofenceEventPayload[
                sizeof(pendingGeofenceEventPayload) - 1
            ] = '\0';
            hasPendingGeofenceEvent = true;
            Serial.println("[MQTT] Evenement geofence garde pour reconnexion.");
        }

        Serial.println("[MQTT] Geofence non envoye : non connecte.");
        return false;
    }

    const String currentGeofenceTopic = geofenceTopic();
    // L'etat est retenu afin qu'un observateur retrouve le dernier controle.
    const bool statusSent = mqttClient.publish(
        currentGeofenceTopic.c_str(),
        payload,
        true
    );

    bool eventSent = true;
    if (isEvent)
    {
        const String eventsTopic = geofenceEventsTopic();
        eventSent = mqttClient.publish(eventsTopic.c_str(), payload, false);

        if (!eventSent)
        {
            strncpy(
                pendingGeofenceEventPayload,
                payload,
                sizeof(pendingGeofenceEventPayload) - 1
            );
            pendingGeofenceEventPayload[
                sizeof(pendingGeofenceEventPayload) - 1
            ] = '\0';
            hasPendingGeofenceEvent = true;
        }
    }

    const bool sent = statusSent && eventSent;
    Serial.println(
        sent
            ? "[MQTT] GEOFENCE ENVOYE"
            : "[MQTT] ECHEC ENVOI GEOFENCE"
    );
    return sent;
}
#else
void initMQTT() {}
void updateMQTT() {}
bool isMQTTConnected() { return false; }
bool sendTelemetryToMQTT(const TelemetryData&) { return false; }
bool sendGeofenceToMQTT(const GeofenceResult&, const char*, bool) { return false; }
#endif
