# Bancs de l'IoT Box V2

Les bancs matériels correspondent aux composants du firmware.
Leurs sketches se téléversent séparément ; les scripts de compilation
ne flashent aucune carte.

| Composant ou fonction | Banc |
| --- | --- |
| INA228 / INA3221 | Registres, calibration, shunts, conflits et pannes simulés dans [tests/refonte](../tests/refonte/run_tests.py) |
| Bus I²C | [Scanner des quatre adresses](I2C/i2c_scanner/i2c_scanner.ino) |
| PZEM-004T V3 10 A | CRC, timeouts, compteur et énergie simulés dans [tests/refonte](../tests/refonte/run_tests.py) ; essai réel dans [le guide](../docs/VALIDATION_V2.md) |
| A7670 | [Dialogue AT manuel](A7670/a7670_at_test/a7670_at_test.ino) |
| NEO-8M | [NMEA, fix et fraîcheur](NEO-8M/README.md) |
| DS1302 | [RTC et pile](DS1302/README.md) |
| LEDs / buzzer | [Indicateurs et alertes](INDICATORS/README.md) |
| Geofence | [Tests de la zone](GEOFENCE/geofence_test/geofence_test.ino) |
| SMS et HMAC | [Banc de conformité avec A7670](SMS/README.md) |

Les tests automatisés utilisent des bus et réponses contrôlés. Ils prouvent
le comportement du code face à ces entrées, pas la précision des mesures,
le câblage ou la livraison d'un SMS par l'opérateur.
