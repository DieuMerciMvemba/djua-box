#include "gps.h"

#include <TinyGPSPlus.h>

#include "../pins.h"
#include "../config.h"

// =====================================================
// NEO-8M : NMEA/TinyGPSPlus, independent of modem and its GNSS.
// =====================================================

static TinyGPSPlus gps;

// UART matériel numéro 2 de l'ESP32
static HardwareSerial gpsSerial(2);

namespace {
// Un résumé régulier permet de diagnostiquer le GPS même sans position valide.
constexpr uint32_t GPS_DIAGNOSTIC_INTERVAL_MS = 5000UL;
uint32_t lastDiagnosticAt = 0;
uint32_t receivedBytesSinceDiagnostic = 0;
uint32_t receivedBytesTotal = 0;

void printFieldAge(bool valid, unsigned long ageMs)
{
    if (!valid)
    {
        Serial.print("inconnu");
        return;
    }

    Serial.print(ageMs <= GPS_MAX_AGE_MS ? "frais, age=" : "perime, age=");
    Serial.print(ageMs);
    Serial.print(" ms");
}

void printGPSDiagnostics(uint32_t now)
{
    const uint32_t elapsed = now - lastDiagnosticAt;
    if (elapsed < GPS_DIAGNOSTIC_INTERVAL_MS) return;

    Serial.print("[GPS] UART RX: ");
    Serial.print(receivedBytesSinceDiagnostic);
    Serial.print(" octets/");
    Serial.print(elapsed);
    Serial.print(" ms, total=");
    Serial.print(receivedBytesTotal);
    Serial.print(" | NMEA: caracteres=");
    Serial.print(gps.charsProcessed());
    Serial.print(" checksum_OK=");
    Serial.print(gps.passedChecksum());
    Serial.print(" checksum_KO=");
    Serial.print(gps.failedChecksum());
    Serial.print(" trames_avec_FIX=");
    Serial.println(gps.sentencesWithFix());

    const bool locationValid = gps.location.isValid();
    const unsigned long locationAge = gps.location.age();
    Serial.print("[GPS] Etat: FIX=");
    if (!locationValid) Serial.print("NON");
    else Serial.print(locationAge <= GPS_MAX_AGE_MS ? "OUI_RECENT" : "PERIME");
    Serial.print(" (age=");
    if (locationValid) Serial.print(locationAge);
    else Serial.print("inconnu");
    Serial.print(" ms, limite=");
    Serial.print(GPS_MAX_AGE_MS);
    Serial.print(" ms) | satellites=");
    if (gps.satellites.isValid()) Serial.print(gps.satellites.value());
    else Serial.print("inconnu");
    Serial.print(" (");
    printFieldAge(gps.satellites.isValid(), gps.satellites.age());
    Serial.print(") | HDOP=");
    if (gps.hdop.isValid()) Serial.print(gps.hdop.hdop() / 100.0, 2);
    else Serial.print("inconnu");
    Serial.print(" (");
    printFieldAge(gps.hdop.isValid(), gps.hdop.age());
    Serial.print(") | altitude=");
    if (gps.altitude.isValid()) Serial.print(gps.altitude.meters(), 1);
    else Serial.print("inconnue");
    Serial.print(" m (");
    printFieldAge(gps.altitude.isValid(), gps.altitude.age());
    Serial.print(") | vitesse=");
    if (gps.speed.isValid()) Serial.print(gps.speed.kmph(), 1);
    else Serial.print("inconnue");
    Serial.print(" km/h (");
    printFieldAge(gps.speed.isValid(), gps.speed.age());
    Serial.print(")\n");

    receivedBytesSinceDiagnostic = 0;
    lastDiagnosticAt = now;
}
} // namespace

// INITIALISATION

void initGPS()
{
    lastDiagnosticAt = millis();
    receivedBytesSinceDiagnostic = 0;
    receivedBytesTotal = 0;

    gpsSerial.begin(
        GPS_BAUD_RATE,
        SERIAL_8N1,
        GPS_RX_PIN,
        GPS_TX_PIN
    );

    Serial.println("[GPS] NEO-8M initialise (UART2 independant).");
    Serial.print("[GPS] RX ESP32 : GPIO ");
    Serial.println(GPS_RX_PIN);

    Serial.print("[GPS] TX ESP32 : GPIO ");
    Serial.println(GPS_TX_PIN);
}

// MISE A JOUR

void updateGPS()
{
    /*
     * Important :
     * TinyGPSPlus doit recevoir continuellement les
     * caracteres NMEA provenant du NEO-8M.
     */
    while (gpsSerial.available() > 0)
    {
        const int received = gpsSerial.read();
        if (received < 0) continue;
        ++receivedBytesSinceDiagnostic;
        ++receivedBytesTotal;
        gps.encode(static_cast<char>(received));
    }

    printGPSDiagnostics(millis());
}

// LECTURE

bool isGPSLocationUsable(
    bool locationValid,
    unsigned long locationAgeMs
)
{
    return
        locationValid &&
        locationAgeMs <= GPS_MAX_AGE_MS;
}

GPSData readGPS()
{
    GPSData data;

    data.latitude = 0.0;
    data.longitude = 0.0;
    data.valid = false;

    const bool locationValid = gps.location.isValid();
    const unsigned long locationAge = gps.location.age();
    data.ageMs = locationAge;
    data.source = djua::LocationSource::NeoGnss;
    data.hdopKnown = gps.hdop.isValid() && gps.hdop.age() <= GPS_MAX_AGE_MS;
    if (data.hdopKnown) data.hdop = gps.hdop.hdop();

    if (!locationValid)
    {
        Serial.println("[GPS] Sans FIX - aucune position valide.");
        return data;
    }

    if (!isGPSLocationUsable(locationValid, locationAge))
    {
        Serial.print("[GPS] FIX ancien/perime (age ");
        Serial.print(locationAge);
        Serial.print(" ms, limite ");
        Serial.print(GPS_MAX_AGE_MS);
        Serial.println(" ms).");
        return data;
    }

    data.latitude = gps.location.lat();
    data.longitude = gps.location.lng();
    data.valid = true;

    Serial.print("[GPS] FIX valide et recent (age ");
    Serial.print(locationAge);
    Serial.println(" ms).");

    return data;
}

// ETAT DU GPS

bool hasGPSFix()
{
    return isGPSLocationUsable(
        gps.location.isValid(),
        gps.location.age()
    );
}
