# Livraison de la refonte IoT Box V2

11 octobre 2026 — branche `codex/refonte-firmware-iot-box-v2`.

Le firmware est intégré et testé sur PC. Il n'a été ni téléversé sur une box,
ni validé avec des mesures physiques ou un SMS opérateur réel. Le dépôt était
propre au début de la mission ; les modifications de cette branche concernent
la refonte, ses bancs, ses tests et sa documentation. Aucun push n'a été effectué.

## Résultat et responsabilités

| Besoin | Implémentation | Vérification disponible |
|---|---|---|
| Batterie, solaire, TV indépendants | Trois instances `Ina228`, résistances, polarités et limites individuelles dans `src/config.h` | Registres, calibration, limites et pannes simulés |
| DC1/DC2/DC3 distincts | `Ina3221`, shunt 10 mΩ par voie, quatre identités DC avec TV | Panne d'une voie sans invalider les autres ; JSON et D3 |
| Bus partagé sans conflits configurés | `I2cBus`, réservation d'adresse et assertions du profil | Collision refusée ; straps physiques à contrôler |
| AC PZEM 10 A | Modbus V3, CRC, acquisition bornée, compteur Wh, PF/fréquence, S=V×I | Réponses, exceptions, CRC, timeout, baisse/saut de compteur |
| UART indépendants | Console UART0, modem UART1, GPS UART2, PZEM logiciel RX35/TX14 | Profils compilés ; essais simultanés réels nécessaires |
| A7670 SMS | Profil AT, LTE/CS et capacité SMS distincts, SMSC, erreurs CMS/CME, retries | Réponses simulées ; banc manuel sans envoi automatique |
| NEO-8M principal | Service de localisation indépendant, source/âge/précision, interface cellulaire optionnelle | Priorité GNSS, fraîcheur, confirmations geofence, refus cellulaire |
| Énergie et données absentes | Fenêtre par voie, complétude persistante pendant la fenêtre, valeurs absentes distinctes de zéro | Gaps, défauts, rollover `millis`, null JSON et flags D3 |
| SMS versionnés | D3 en deux SMS signés, insertion atomique de la paire dans le journal existant | Référence Python indépendante, HMAC, GSM-7, NVS, reboot, saturation et fin des séquences |
| Compatibilité persistante | Structures NVS/version/namespace conservés ; anciens SMS relus avec leur codec | Textes historiques inchangés avant/après reboot et retry |

Le chemin principal est : pilote → acquisition identifiée → énergie de la voie
→ télémétrie V3 → deux SMS D3 signés → file NVS → moteur AT → modem.
Le GPS et le DS1302 fournissent la localisation et l'heure indépendamment du modem.
MQTT et HTTP sont des profils de test exclusifs du transport SMS.

## Fichiers à connaître

- `src/config.h` : activation des variantes physiques, shunts et limites.
- `src/firmware_v2.cpp` : assemblage des pilotes, cycles d'acquisition et fenêtres.
- `src/drivers/` : pilotes portables et interfaces de bus, testables sans ESP32.
- `src/location/location.h` : sélection et qualité de localisation.
- `src/communication/gsm.cpp` : dialogue AT A7670/SIM800 conservé pour les bancs.
- `src/communication/d3_codec.cpp`, `d2_store.cpp` : contrat et persistance.
- `src/telemetry/json_v3.h` : JSON avec quatre ports, diagnostics et valeurs nulles.
- `tests/refonte/` : simulations des composants et comparaison indépendante du protocole.

## Vérifications exécutées

Environnement : Windows, MSVC, Python 3.12, Arduino ESP32 3.3.12,
ArduinoJson 7 et EspSoftwareSerial 8.1.0 installé avec autorisation.

| Vérification | Résultat |
|---|---|
| `python tests/refonte/run_tests.py` | PASS : pilotes, modem, localisation/geofence, D3, persistance, référence Python et JSON réel ArduinoJson |
| `python tests/sms/run_tests.py` | PASS : 7 tests Python et 8 groupes C++ historiques |
| `python tests/dc_load/run_tests.py` | PASS : 4 variantes historiques |
| Banc solaire PC | PASS : 4 variantes |
| Banc indicateurs PC | PASS : 7 variantes, dont MQTT/HTTP |
| Banc provisionnement SMS Python | PASS : 12 tests |
| API, Python de `api/.venv`, découverte de `api/tests` | PASS : 5 tests, dont conservation des quatre identités DC et de la qualité V3 |
| Compilation ESP32 SMS production | PASS : 393891 octets flash, 29648 octets RAM |
| Compilation ESP32 MQTT_TEST | PASS : 979403 octets flash, 51660 octets RAM |
| Compilation ESP32 HTTP_TEST | PASS : 1008475 octets flash, 52084 octets RAM |
| Compilation ESP32 SMS_UART_PROVISIONING_COMPILE_ONLY, périphériques V2 et D3 activés | PASS : 400247 octets flash, 29920 octets RAM ; configuration de banc uniquement |
| Compilation ESP32 SMS_DC_DISABLED | PASS : 393891 octets flash, 29648 octets RAM |
| Compilation ESP32 SMS_LEGACY_HARDWARE | PASS : 408899 octets flash, 77528 octets RAM |
| Banc manuel A7670 compilé avec `GSM_HARDWARE_CONFIGURED=1` | PASS : 269032 octets flash, 22188 octets RAM ; aucune communication réelle |

Les simulations exercent le code des pilotes et du journal, avec des entrées
contrôlées et des écritures défaillantes. Elles ne mesurent pas la précision
des capteurs ni la fiabilité des niveaux électriques. Les données et clés des
vecteurs de test sont publiques et réservées aux tests ; aucun secret de
production n'a été ajouté ou remplacé. Le HMAC de production reste obligatoire.
Les compilations affichent des avertissements déjà présents : API ArduinoJson
anciennes dans les branches historiques, dimension du tampon d'affichage RTC
et fallthrough de TinyGPSPlus, ainsi que conversions d'enums de l'ancienne
bibliothèque Adafruit INA219. La compilation n'est pas présentée comme exempte
d'avertissements.

## Limites et conditions d'activation

Les indicateurs `V2_*_CONFIGURED`, `PZEM_HARDWARE_CONFIGURED` et
`GSM_HARDWARE_CONFIGURED` sont à zéro tant que les variantes physiques ne sont
pas confirmées. Les limites DC et puissances admissibles de shunt proposées
restent provisoires. Confirmer les résistances effectives, notamment les shunts
éventuellement déjà présents sur les modules.

Le pilote PZEM exige **V3.0/Modbus et la variante 10 A à shunt intégré**.
Vérifier interface, niveaux 3,3 V et protections secteur. L'UART logiciel doit
être éprouvé avec GNSS et modem simultanément actifs. GPIO35 n'a pas de pull-up
interne ; GPIO14 est réservé au TX PZEM et ne peut plus servir de PWRKEY modem.

`D3_RECEIVER_ENABLED=0` bloque la création de nouveaux SMS D3 jusqu'à adaptation
du récepteur externe. Deux SMS coûtent deux transmissions par fenêtre ; la
réserve périodique actuelle conserve deux fenêtres D3, avec la réserve d'urgence
inchangée. Les anciennes entrées peuvent être envoyées si le modem est activé.
Un acquittement modem n'est jamais une preuve de réception ou d'assemblage externe.
Ne pas effacer la NVS ni réinitialiser les séquences lors de la mise à jour.
La compatibilité couvre la lecture des anciennes entrées par ce nouveau
firmware. Un binaire antérieur à D3 peut refuser une file contenant D3 ; ne pas
l'effacer pour contourner ce refus. `IOT_HARDWARE_V2=0` sélectionne l'ancien
matériel dans cette nouvelle version, pas le décodeur d'un ancien binaire.

Le fournisseur cellulaire reste un point d'extension, sans requêtes LBS actives.
La précision métrique du GNSS reste inconnue avec les seules phrases NMEA
actuellement décodées ; HDOP est transmis séparément en JSON. Le DS1302 et
l'heure locale GMT+1 sont conservés. Vérifier l'heure, la pile et le fix GNSS.
L'initialisation RTC à la date de compilation est désactivée en SMS production.
Les profils de test MQTT/HTTP conservent l'ancienne option d'initialisation
automatique ; la désactiver (`RTC_AUTO_INITIALIZE_IF_INVALID=0`) pour vérifier
une RTC réellement indisponible. Leur heure de compilation n'est pas une référence.

Aucun récepteur externe n'a été modifié. Son assemblage D3 est spécifié dans
[SMS_D3.md](SMS_D3.md). Le backend HTTP externe doit également accepter le JSON
V3 avant utilisation. Les essais réels détaillés sont dans
[VALIDATION_V2.md](VALIDATION_V2.md), le brochage et les décisions dans
[REFONTE_PHASE0.md](REFONTE_PHASE0.md).

## Lots Git

1. `4a02162` : audit et brochage, phase 0.
2. `6fd07a7` : pilotes INA228/INA3221 et simulations.
3. `87974f5` : PZEM Modbus et énergie de compteur.
4. `731d032` : A7670 SMS et localisation indépendante.
5. `82327b1` : D3 et paire persistante compatible.
6. Lot final : intégration V2, non-régression et documentation.

Les bancs historiques sélectionnent explicitement `IOT_HARDWARE_V2=0`.
Le document [FONCTIONNEMENT_ACTUEL.md](../FONCTIONNEMENT_ACTUEL.md) décrit le
profil actuel ; les procédures anciennes restent consultables pour l'ancien matériel.
