# Livraison IoT Box V2

11 octobre 2026 — branche `codex/refonte-firmware-iot-box-v2`.

Le firmware utilise uniquement les nouveaux composants. Les pilotes,
paramètres, branches conditionnelles et bancs de matériel remplacé sont retirés.
La documentation décrit l'architecture actuelle et la reprise des SMS stockés.

## Composants et fonctionnement

- Trois INA228 : BAT, SOL et TV, calibrés indépendamment.
- Un INA3221 : DC1/DC2/DC3, shunt 10 mΩ par voie.
- PZEM-004T V3 10 A : AC, CRC Modbus, compteur Wh, PF et fréquence.
- A7670 uniquement : UART1 RX32/TX13, 115200 bauds.
- NEO-8M : UART2 RX16/TX17 ; DS1302 DATA26/CLOCK25/CE27.
- PZEM sur UART logiciel RX35/TX14, 9600 bauds ; console USB indépendante.

`iot_box.ino` lance directement `src/firmware_v2.cpp`.
Acquisition → validité/âge → énergie par voie → modèle V3 →
deux SMS D3 signés → insertion atomique NVS → A7670.
MQTT_TEST et HTTP_TEST utilisent le même matériel et le même JSON V3.

La lecture des SMS D2T/D2T2/D2E déjà persistés est conservée sans pilote matériel
alternatif. Namespace, blobs, séquences, signatures et retries restent compatibles.

## Fichiers importants

- `src/config.h` : uniquement les paramètres des composants V2.
- `src/firmware_v2.cpp` : acquisitions, localisation et fenêtres.
- `src/drivers/` : INA228, INA3221, bus partagé et PZEM.
- `src/telemetry/` : modèle V3 et JSON unique, quatre identités DC.
- `src/status/device_status.cpp` : alertes des six voies DC et de l'AC.
- `src/communication/gsm.cpp` : moteur AT A7670 sans sélection de modem.
- `tests/refonte/` et `tests/sms/` : simulations et contrats persistés.
- `TEST UNITAIRE/` : bancs correspondants aux composants actuels.

## Vérifications exécutées après nettoyage

| Vérification | Résultat |
| --- | --- |
| `python tests/refonte/run_tests.py` | PASS : registres, calibration, PZEM, A7670, localisation/geofence, D3, NVS, référence Python et JSON ArduinoJson réel |
| `python tests/sms/run_tests.py` | PASS : 7 tests Python et 8 groupes C++ |
| Indicateurs sur PC | PASS : 7 variantes, avec pannes/récupérations SOL/DC1/DC2/DC3/TV |
| Provisionnement SMS Python | PASS : 12 tests, aucune clé affichée et aucun envoi |
| Compilation des cinq profils ESP32 | PASS : SMS production, MQTT, HTTP, tous capteurs/D3/provisionnement activés, DC désactivé |
| Bancs matériels actualisés, compilation sans flash | PASS : scanner I²C, NEO-8M, DS1302, HMAC/A7670 |
| Liens Markdown des guides | PASS : 18 fichiers, aucun lien local cassé |
| Recherche des composants remplacés dans les sources et guides | PASS : aucune référence résiduelle |

Tailles des profils compilés :

| Profil | Flash (octets) | RAM (octets) |
| --- | ---: | ---: |
| SMS_PRODUCTION | 393695 | 29640 |
| MQTT_TEST | 977815 | 51660 |
| HTTP_TEST | 1007047 | 52076 |
| SMS_UART_PROVISIONING_COMPILE_ONLY | 400083 | 29920 |
| SMS_DC_DISABLED | 393695 | 29640 |

Tailles des bancs compilés :

| Banc | Flash (octets) | RAM (octets) |
| --- | ---: | ---: |
| Scanner I²C | 293796 | 23500 |
| NEO-8M | 275064 | 22420 |
| DS1302 | 277644 | 22132 |
| HMAC/A7670 | 329632 | 25828 |

Environnement : Windows/MSVC/Python, Arduino ESP32 3.3.12,
ArduinoJson 7 et EspSoftwareSerial 8.1.0.
La compilation signale des avertissements d'affichage RTC,
d'API ArduinoJson dans les événements et de fallthrough TinyGPSPlus.
Le banc RTC signale aussi une fonction inutilisée. Aucune erreur de compilation.

## Mise en service restante

Les confirmations des capteurs et du modem restent à zéro jusqu'aux contrôles
physiques. Vérifier shunts, adresses, limites thermiques, interfaces et alimentation.
Le PZEM exige V3.0/Modbus et la variante 10 A. Éprouver l'UART logiciel
avec trafic GNSS/LTE simultané. GPIO14 ne peut pas piloter PWRKEY.

`D3_RECEIVER_ENABLED=0` bloque les nouvelles paires jusqu'à adaptation
du récepteur externe. Deux parties nécessitent deux transmissions.
Un acquittement modem ne prouve pas leur réception ni leur assemblage.
Ne pas effacer la NVS ou réinitialiser les séquences lors d'une mise à jour.

Aucun téléversement, SMS réel ou push n'a été effectué.
Les tests simulés ne prouvent pas la précision physique des capteurs.

Brochage : [REFONTE_PHASE0.md](REFONTE_PHASE0.md).
Essais matériels : [VALIDATION_V2.md](VALIDATION_V2.md).
Récepteur : [SMS_D3.md](SMS_D3.md).
