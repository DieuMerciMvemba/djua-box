# Indication locale DJUA

## Cablage et essai materiel

| Sortie | GPIO ESP32 | Cablage |
|---|---:|---|
| SYSTEM verte | 18 | GPIO -> resistance 220 a 330 ohms -> anode LED ; cathode -> GND |
| COMM bleue | 19 | GPIO -> resistance 220 a 330 ohms -> anode LED ; cathode -> GND |
| ALERT rouge | 23 | GPIO -> resistance 220 a 330 ohms -> anode LED ; cathode -> GND |
| Buzzer | 32 | GPIO -> circuit de commande adapte ; GND commun |

Une resistance distincte est necessaire pour chaque LED. Les LEDs sont actives
a HIGH par defaut (`LED_ACTIVE_HIGH`). Verifier la polarite anode/cathode et
l'eclairement avec la resistance choisie.

Le type et le courant du buzzer ne sont pas encore connus. Verifier sa fiche
technique et la compatibilite de son entree avec 3,3 V. Si son courant depasse
ce que le GPIO ESP32 peut fournir, utiliser un transistor/MOSFET et une
alimentation adaptee, avec masse commune, au lieu de l'alimenter depuis GPIO32.
Prevoir la protection appropriee si la charge est inductive. Un transistor de
commande ou un module peut inverser le niveau actif.

Ouvrir `indicators_test/indicators_test.ino`, choisir **ESP32 Dev Module**, puis
televerser manuellement sur le materiel de test. Moniteur serie : 115200 bauds.
La sequence teste vert fixe 1 s, heartbeat trois cycles, bleu lent/rapide/fixe,
rouge warning/critical/geofence, puis les quatre sons. A la fin, seul le
heartbeat continue. Envoyer `R` pour recommencer. Aucun `delay()` dans le sketch.

Par defaut `BUZZER_ENABLED=0` : le sketch annonce les etapes sonores mais ne
configure pas GPIO32 et n'emet aucun son. Pour tester un composant identifie,
definir dans `src/config.h` (ou par options de compilation) :

```cpp
#define BUZZER_ENABLED 1
#define BUZZER_PASSIVE 0      // 0 : actif ; 1 : passif avec tonalite 2000 Hz
#define BUZZER_ACTIVE_HIGH 1  // 0 si l'etage de commande est actif a LOW
```

L'API passive utilise `ledcAttach(pin, frequency, resolution)` et
`ledcWriteTone(pin, frequency)` du coeur **esp32:esp32 3.3.11** installe. Ce code
ne vise pas l'ancienne API LEDC par canal du coeur 2.x. Le niveau de repos est
adapte aux deux polarites. L'absence physique de buzzer ou de LED ne bloque pas
le demarrage. Une erreur d'allocation timer/LEDC desactive le son avec un log.

## Audit de l'existant

Depot local `toussaintmadimba-cmyk/DJUA`, branche `main`. Avant modification :
`iot_box.ino`, `pins.h`, `config.h`, capteurs, communication, geofencing et
telemetrie inspectes. Aucun usage des GPIO 18/19/23/32 dans le firmware actif ;
les emplacements GSM et microSD sont seulement des commentaires sans broches.

| Module | API utilisee ou identifiee |
|---|---|
| INA219 batterie | `initPowerSensor`, `readPowerSensor`, `isPowerSensorAvailable` ; recuperation I2C interne |
| INA219 solaire | `initSolarPowerSensor`, `readSolarPowerSensor`, `isSolarPowerSensorAvailable` ; diagnostic par `recordSolarStatus` |
| AC | `initACPowerSensor`, `readACPowerSensor`, `isACPowerSensorAvailable`, console de calibration |
| GPS | `initGPS`, `updateGPS`, `readGPS`, `hasGPSFix`, `isGPSLocationUsable` |
| RTC | `initRTC`, `readRTC`, `isRTCAvailable`, `formatRTCTimestamp` |
| Wi-Fi | `initInternet`, `updateInternet`, `isInternetConnected` ; ajout de `isInternetConnecting` |
| MQTT | `initMQTT`, `updateMQTT`, `isMQTTConnected`, `sendTelemetryToMQTT`, `sendGeofenceToMQTT` |
| HTTP | `sendTelemetryToBackend` retourne le succes HTTP 2xx |
| Geofence | `initGeofence`, `updateGeofence`, `GeofenceResult.exitEvent/enterEvent` |
| Telemetrie | `buildTelemetry` recoit aussi le solaire ; `TelemetryData.solarMeasurementValid` distingue une mesure invalide de 0 W |

Broches conservees : INA219 SDA21/SCL22, GPS RX16/TX17, DS1302 DATA25/CLOCK26/CE27,
ZMPT101B 34, ACS712 35. Aucun algorithme capteur, format de message, API backend
ou calcul geofence modifie. MQTT et HTTP conserves ; pas d'integration GSM.

## Architecture et decisions

`iot_box.ino` transmet les resultats existants a `src/status/device_status`.
Cette couche memorise les anomalies, calcule leur priorite et appelle
`src/indicators/indicators`. Le driver ne connait ni capteur ni reseau.
Le futur GSM pourra fournir les memes booleens reseau/transport a la decision.

SYSTEM, COMM et ALERT sont trois enums independants. Le timer du buzzer ne
pilote **jamais** les LEDs. `updateIndicators()` est appele avant tout retour
anticipe de `loop()` et aux limites des operations synchrones.

| Etat LED | Pattern ON / OFF |
|---|---|
| SYSTEM_BOOTING | 200 / 200 ms |
| SYSTEM_RUNNING | 100 / 1900 ms |
| COMM_OFFLINE | eteinte |
| COMM_CONNECTING | 500 / 500 ms |
| COMM_TRANSPORT_PENDING | 150 / 150 ms |
| COMM_ONLINE | fixe |
| ALERT_NONE | eteinte |
| ALERT_WARNING | 500 / 500 ms |
| ALERT_CRITICAL, ALERT_GEOFENCE | 150 / 150 ms |

La connexion Wi-Fi initiale est maintenant asynchrone. Une tentative dure au
maximum 15 s, suivie de 10 s d'attente avant nouvel essai. CONNECTING indique
une tentative en cours ; OFFLINE indique l'absence de reseau entre les essais.
Un reseau connecte seul donne TRANSPORT_PENDING. MQTT connecte (sans dernier
echec de publication) ou un succes HTTP recent donne ONLINE. Apres un echec
MQTT, une publication reussie retablit son utilisabilite. Une confirmation HTTP expire apres 30 s par defaut,
au prochain echec HTTP ou a la perte reseau.

MQTT utilise le succes de connexion/publication expose par PubSubClient ; ce
n'est pas un accuse de traitement du backend. Aucun accuse applicatif n'existe
dans le protocole actuel. HTTP 2xx fournit une confirmation cote endpoint.

Priorite : **GEOFENCE > CRITICAL > WARNING > NONE**.

- WARNING : RTC invalide apres son initialisation/recuperation ; trois mesures
  INA219 batterie invalides consecutives ; trois mesures AC invalides consecutives ;
  trois mesures solaires invalides ou indisponibles consecutives si solaire active ;
  trois cycles de telemetrie sans aucun succes ; MQTT absent pendant 30 s
  alors que le reseau est disponible (meme si HTTP fonctionne).
- CRITICAL : trois constats d'indisponibilite INA219 batterie consecutifs, ou INA219 batterie et
  AC tous deux invalides pendant au moins trois mesures. Une indisponibilite
  constatee a l'initialisation compte comme premier constat INA219 batterie.
  Le solaire seul ne donne jamais CRITICAL ; une production nulle valide n'est pas une panne.
- Un succes de mesure reinitialise son compteur ; un succes MQTT **ou** HTTP
  reinitialise le compteur commun de telemetrie. AC desactive ne compte pas.
- Pas d'alerte batterie metier active. `BATTERY_MIN_VALID_V` reste uniquement
  une limite de validite. Activer `BATTERY_ALERTS_ENABLED` exige de renseigner
  explicitement `BATTERY_WARNING_V` et `BATTERY_CRITICAL_V`.
- GPS sans fix ne produit aucune alerte. EXIT confirme memorise l'alarme meme
  si le GPS devient inutilisable. ENTER confirme la leve, recalcule les autres
  alertes et emet INFO une fois. Un demarrage directement hors zone ne produit
  pas EXIT dans le geofencing existant : ce comportement est conserve.

Les compteurs capteurs/telemetrie sont exprimes en cycles effectifs, normalement
10 s chacun, et non en duree garantie. Les reglages sont dans `src/config.h`.

## Buzzer

| Evenement | Sequence |
|---|---|
| INFO | 100 ms ON |
| WARNING | 100 ON, 150 OFF, 100 ON |
| CRITICAL | 100 ON, 150 OFF, 100 ON, 150 OFF, 100 ON |
| GEOFENCE | 700 ON, 200 OFF, 150 ON, 150 OFF, 150 ON |

La machine d'etat utilise `millis()` et un timer logiciel ESP periodique de
10 ms en contexte tache, sans `delay()` ni attente active. Elle continue
pendant les appels synchrones existants de la boucle. Les commandes sont
transmises sous une courte section critique ; les ecritures GPIO/LEDC sont
effectuees hors de cette section et sans logs dans le callback.

Un evenement sonore plus prioritaire peut interrompre un son moins important.
Un evenement egal ou inferieur ne redemarre pas le son en cours. La couche de
decision ne rejoue pas un niveau persistant. Aucun rappel periodique configure.
ENTER annule le son precedent et demande uniquement INFO ; la LED conserve
l'eventuelle alerte capteur restante. BUZZER_NONE annule au prochain tick.
Si un tick prend du retard, les phases deja ecoulees sont sautees.

## Tests et limites

Validation logicielle effectuee le 20 septembre 2026, coeur esp32:esp32 3.3.11 :

| Verification | Resultat |
|---|---|
| Firmware complet, ESP32 Dev Module, warnings all, jobs 1 | Compilation reussie ; flash 1 021 784 octets (77 %), RAM statique 100 216 octets (30 %) |
| Sketch materiel, buzzer desactive | Compilation reussie ; flash 273 432 octets |
| Sketch materiel, buzzer actif | Compilation reussie ; flash 274 176 octets |
| Sketch materiel, buzzer passif / API LEDC 3.x | Compilation reussie ; flash 283 159 octets |
| Tests natifs, desactive / actif haut / actif bas / passif haut / passif bas | Les cinq executions reussissent |
| Revue GPIO, absence de delay dans les nouveaux modules et controle du diff | Verifies |

La compilation complete signale des avertissements dans du code preexistant :
API ArduinoJson depreciees, formatage du log RTC, ainsi que les bibliotheques
TinyGPSPlus et Adafruit_INA219. Aucun avertissement recu pour les nouveaux
modules ou pour les trois variantes du sketch materiel.

Tests natifs : depuis une invite developpeur Visual Studio, lancer
`host/run_tests.cmd`. Les executables sont crees dans `%TEMP%/djua-indicators-host`.
Les fichiers de production indicators, device_status et geofence sont executes
avec horloge, GPIO et timer simules. Les cinq variantes (desactive, actif
haut/bas, passif haut/bas) couvrent les timings, le debordement `millis()`, la
deduplication, les priorites, la recuperation capteurs, HTTP perime, la perte
reseau et les transitions geofence reelles. Ces tests ne simulent pas les
courses entre coeurs ni les caracteristiques electriques.

Compilation ESP32 :

```text
arduino-cli compile --fqbn esp32:esp32:esp32 --warnings all --jobs 1 .
arduino-cli compile --fqbn esp32:esp32:esp32 --warnings all --jobs 1 "TEST UNITAIRE/INDICATORS/indicators_test"
```

Pour compiler les variantes actives sans changer config.h, ajouter
`--build-property "compiler.cpp.extra_flags=-DBUZZER_ENABLED=1 -DBUZZER_PASSIVE=1"`
(mettre PASSIVE=0 pour le buzzer actif ; ACTIVE_HIGH=0 pour la polarite inverse).

`compile_variants.ps1 -ArduinoCli <chemin-vers-arduino-cli>` compile le sketch
dans les trois modes desactive/actif/passif. Il prepare des copies temporaires
avec defines locaux et chemins d'inclusion absolus, sans modifier config.h ni
forcer une recompilation du coeur pour chaque jeu de defines.

Les acquisitions INA219/AC, la correction RTC et les appels MQTT/HTTP restent
synchrones. Les LEDs peuvent donc figer pendant une acquisition AC (environ
1 s) ou un timeout reseau. C'est une limite existante de la cadence de boucle :
le heartbeat ne masque pas ces pauses. La nouvelle indication n'ajoute aucune
attente et le buzzer continue sur son timer, mais elle ne rend pas les anciens
pilotes asynchrones. Le clignotement BOOT peut etre tres bref si le demarrage
se termine avant 200 ms ; la correction RTC peut retarder une phase.

Aucun televersement ni essai physique effectue dans cette intervention.
Le type actif/passif, la polarite, la consommation et le son du buzzer restent
a confirmer sur le materiel reel. Les resistances et le cablage LED restent
egalement a verifier physiquement.
