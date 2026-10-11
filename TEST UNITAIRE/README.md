# Tests unitaires et tests de modules

Ce dossier contient des tests autonomes. Il est volontairement séparé du firmware principal : aucun fichier présent ici n'est inclus par `iot_box.ino` et les tests ne modifient donc pas le fonctionnement actuel de l'IoT Box.

## Modules couverts

| Module | État |
| --- | --- |
| Indicateurs LED et buzzer | Sketch materiel et tests automatises dans [`INDICATORS`](INDICATORS/README.md) |
| INA219 solaire | Scanner I2C, sketch solaire et tests automatises dans [`INA219_SOLAR`](INA219_SOLAR/README.md) |
| INA219 charges DC | Shunt externe configurable, sketch et tests automatisés dans [`INA219_DC`](INA219_DC/README.md) |
| SMS D2T2 / SIM800C | Banc ESP32 HMAC, provisionnement et retries dans [`SMS`](SMS/README.md) |
| Horloge RTC DS1302 | Premier test disponible dans [`DS1302`](DS1302/README.md) |
| GPS NEO-6M | Test matériel disponible dans [`NEO-6M`](NEO-6M/README.md) |
| Geofencing | Test à coordonnées simulées dans [`GEOFENCE/geofence_test`](GEOFENCE/geofence_test/geofence_test.ino) |
| ZMPT101B + ACS712-30A | Mesure réelle W/VA/PF et calibration persistante dans [`ZMPT101B_ACS712`](ZMPT101B_ACS712/README.md) |

## Organisation retenue

Chaque module possède son propre dossier avec :

- un sketch Arduino autonome contenant sa configuration locale ;
- les instructions de câblage et d'exécution ;
- la liste des contrôles effectués.

Les futurs tests de modules pourront être ajoutés ici sans modifier les sources de production.
