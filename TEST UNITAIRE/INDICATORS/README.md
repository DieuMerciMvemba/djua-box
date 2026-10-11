# Indicateurs et alertes IoT Box V2

`src/status/device_status.cpp` transforme l'état des composants et du transport
en alertes. `src/indicators/indicators.cpp` pilote les LEDs et le buzzer.
Le service du buzzer est cadencé par un timer ESP32.

## Brochage

| Fonction | GPIO |
| --- | --- |
| Système | 18 |
| Communication | 19 |
| Alerte | 23 |
| Buzzer | 33 |

Les options de polarité, buzzer actif/passif et temporisations sont dans
`src/config.h`. Les broches de mesure sont indépendantes : I²C21/22,
PZEM35/14, GPS16/17, DS1302 DATA26/CLOCK25/CE27, A7670 RX32/TX13.

## États suivis

`recordV2SensorStatus` reçoit les présences et validités dans l'ordre
BAT, SOL, DC1, DC2, DC3, TV, puis la validité AC et RTC.
Seules les voies confirmées dans la configuration comptent comme des
capteurs attendus.

Les échecs consécutifs déclenchent un avertissement au seuil configuré.
Une indisponibilité batterie répétée ou les défauts batterie et AC combinés
déclenchent l'état critique. La geofence est prioritaire.
Les seuils métier de tension batterie sont optionnels.

Le transport suit séparément réseau, MQTT/HTTP et SMS. Un SMS accepté par le
modem ne constitue pas un acquittement du récepteur.

## Tests sur PC

Dans une invite développeur Visual Studio, à la racine du dépôt :

```cmd
"TEST UNITAIRE\INDICATORS\host\run_tests.cmd"
```

Les sept variantes couvrent buzzer désactivé, actif et passif, polarités,
profils HTTP/MQTT, temporisations, wrap de millis, geofence et récupération.
Les pannes SOL/DC1/DC2/DC3/TV sont injectées séparément dans les nouvelles
interfaces, avec tous les capteurs confirmés pour ces essais.

Le sketch [indicators_test](indicators_test/indicators_test.ino) sert à
observer physiquement LEDs et buzzer. Il ne valide pas les capteurs.
