# Concepts utiles dans l'IoT Box V2

## Configuration et architecture matérielle

**Rencontré dans :** `src/config.h` et `iot_box.ino`.

**Pourquoi il existe ici :** les paramètres définissent adresses, shunts,
limites et confirmations des composants. Le programme utilise une seule
architecture matérielle, avec choix du transport SMS/MQTT/HTTP.

**À retenir :** un `#define` agit à la compilation. Une confirmation à zéro
désactive une voie en attente de validation ; elle ne sélectionne pas un autre capteur.

**Exemple du projet :** `V2_BATTERY_CONFIGURED` autorise l'instance INA228 batterie.

## Bus I²C partagé

**Rencontré dans :** `src/drivers/i2c_bus.h` et `src/pins.h`.

**Pourquoi il existe ici :** BAT, SOL, TV et le circuit trois voies partagent
SDA21/SCL22 sans initialiser chacun leur propre bus.

**À retenir :** les adresses doivent être distinctes et correspondre aux straps réels.

**Exemple du projet :** INA228 à 0x40/0x41/0x44 ; INA3221 à 0x42.

## Shunt et calibration

**Rencontré dans :** `src/drivers/ina228.cpp`, `ina3221.cpp` et `src/config.h`.

**Pourquoi il existe ici :** le capteur déduit le courant de la chute de tension
sur une résistance connue. Chaque voie utilise sa résistance effective.

**À retenir :** 30 A/75 mV donne 2,5 mΩ ; à 30 A la dissipation atteint 2,25 W.
La calibration logicielle ne prouve pas la tenue thermique.

**Exemple du projet :** les trois INA228 ont des paramètres indépendants.

## UART logiciel

**Rencontré dans :** `src/firmware_v2.cpp` et `src/pins.h`.

**Pourquoi il existe ici :** les trois UART matériels servent à la console,
à l'A7670 et au NEO-8M. EspSoftwareSerial reçoit le PZEM sur GPIO35 et émet sur GPIO14.

**À retenir :** la réception dépend du traitement des fronts et doit être vérifiée
sous trafic GPS/modem simultané.

**Exemple du projet :** PZEM à 9600 bauds, sans partager UART1 ou UART2.

## Validité et énergie de fenêtre

**Rencontré dans :** `src/telemetry/energy_window.h` et `json_v3.h`.

**Pourquoi il existe ici :** une lecture correcte après une panne ne récupère
pas l'énergie manquante pendant cette panne.

**À retenir :** `valid` et `energy_complete` sont séparés ; une absence devient
null en JSON. Un vrai zéro conserve sa validité.

**Exemple du projet :** chaque entrée DC1/DC2/DC3/TV porte ses propres flags.

## NVS et retries immuables

**Rencontré dans :** `src/communication/d2_store.cpp` et `sms_scheduler.h`.

**Pourquoi il existe ici :** la file et les séquences survivent aux redémarrages ;
le retry doit représenter le même message au récepteur.

**À retenir :** ne pas effacer la NVS ni réutiliser une séquence.
Les textes stockés restent signés selon leur version.

**Exemple du projet :** `enqueueV3` insère atomiquement les deux parties D3.

## HMAC et acquittement modem

**Rencontré dans :** `src/communication/d3_codec.cpp` et `gsm.cpp`.

**Pourquoi il existe ici :** le récepteur vérifie l'authenticité des octets
du message avec la clé propre à l'appareil.

**À retenir :** HMAC ne chiffre pas. `+CMGS` puis `OK` signifient acceptation
par le modem ; le récepteur doit encore recevoir et vérifier les deux parties.

**Exemple du projet :** chaque SMS D3 porte sa propre séquence et signature.
