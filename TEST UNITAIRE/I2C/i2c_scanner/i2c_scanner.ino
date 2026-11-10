
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>

// GPS NEO-8M
#define GPS_RX 16
#define GPS_TX 17
#define GPS_BAUD 9600

TinyGPSPlus gps;
HardwareSerial GPS_Serial(2);

unsigned long lastDisplay = 0;

void setup() {
  Serial.begin(115200);

  GPS_Serial.begin(
    GPS_BAUD,
    SERIAL_8N1,
    GPS_RX,
    GPS_TX
  );

  Serial.println();
  Serial.println("================================");
  Serial.println("     TEST GPS NEO-8M / ESP32");
  Serial.println("================================");
  Serial.println("RX : GPIO 16");
  Serial.println("TX : GPIO 17");
  Serial.println("Attente du signal GPS...");
}

void loop() {

  // Lecture permanente des données GPS
  while (GPS_Serial.available()) {
    gps.encode(GPS_Serial.read());
  }

  // Affichage toutes les secondes
  if (millis() - lastDisplay >= 1000) {
    lastDisplay = millis();

    Serial.println("\n---------- GPS STATUS ----------");

    Serial.print("Satellites : ");
    if (gps.satellites.isValid())
      Serial.println(gps.satellites.value());
    else
      Serial.println("Inconnus");

    Serial.print("HDOP : ");
    if (gps.hdop.isValid())
      Serial.println(gps.hdop.hdop());
    else
      Serial.println("Inconnu");

    if (gps.location.isValid()) {

      Serial.println("LOCALISATION : FIX");

      Serial.print("Latitude  : ");
      Serial.println(gps.location.lat(), 6);

      Serial.print("Longitude : ");
      Serial.println(gps.location.lng(), 6);

      Serial.print("Altitude  : ");
      Serial.print(gps.altitude.meters());
      Serial.println(" m");

      Serial.print("Vitesse   : ");
      Serial.print(gps.speed.kmph());
      Serial.println(" km/h");

      Serial.print("Date      : ");
      if (gps.date.isValid()) {
        Serial.printf("%02d/%02d/%04d\n",
          gps.date.day(),
          gps.date.month(),
          gps.date.year());
      } else {
        Serial.println("Invalide");
      }

      Serial.print("Heure UTC : ");
      if (gps.time.isValid()) {
        Serial.printf("%02d:%02d:%02d\n",
          gps.time.hour(),
          gps.time.minute(),
          gps.time.second());
      } else {
        Serial.println("Invalide");
      }

    } else {
      Serial.println("LOCALISATION : NO FIX");
      Serial.println("Attente de satellites...");
    }

    Serial.println("--------------------------------");
  }
}
