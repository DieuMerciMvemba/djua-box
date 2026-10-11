# Fonctionnement actuel de l'IoT Box

Mise à jour du 7 octobre 2026 : profil **SMS_PRODUCTION**, télémétrie **D2T2**
et événements **D2E**, avec troisième INA219 pour les charges DC.
Référence DJUA_SMS : `c5835077b2b6e8418e34898013cacc0892c12e25`.
Le [guide SMS D2T2](docs/SMS_D2T2.md) décrit le provisionnement, la persistance,
les limites de livraison et les tests. Émetteur SIM800C, récepteur SIM868.
Le GSM est configuré sur UART1 RX32/TX13 après validation du banc par l'utilisateur.
L'envoi nécessite le provisionnement propre au firmware principal et son essai réel.
Le troisième INA219 physique n'est pas encore disponible : les mesures DC restent
absentes avec `DC_LOAD_SHUNT_CONFIGURED=0`, sans bloquer le transport SMS.

Ce document décrit le comportement réellement implémenté dans le dépôt. Le système est composé de deux parties :

- le firmware Arduino/ESP32, qui lit les capteurs et publie la télémétrie ;
- l'API FastAPI, qui écoute MQTT et met les données à disposition en HTTP et WebSocket.

## Vue d'ensemble

```text
INA219 BAT 0x40 ─┐
INA219 SOL 0x41 ─┤
INA219 DC 0x44* ─┤
ZMPT101B+ACS712 ─┤
NEO-6M ──────────┼──> ESP32 ──SMS D2T2/D2E──> SIM868/DJUA_SMS ──MQTT──> backend
DS1302 ──────────┘       │
                        └─ profils de test séparés : MQTT → API FastAPI, ou POST HTTP
```

Chemin actif des données :

1. L'ESP32 mesure la batterie `0x40`, le solaire `0x41` et la charge DC `0x44` sur le même bus. L'adresse DC est proposée et le shunt doit encore être choisi/configuré.
2. Il échantillonne ensemble le ZMPT101B et l'ACS712-30A pour mesurer la charge AC.
3. Il récupère la dernière position valide et récente décodée depuis le GPS NEO-6M.
4. Il vérifie localement la geofence toutes les 5 secondes.
5. Il lit la date et l'heure locale conservées par le DS1302.
6. En mode test, toutes les 10 secondes environ, il construit un objet de télémétrie.
7. En production, les acquisitions alimentent une fenêtre de 30 minutes ; le D2T2 construit est signé et enregistré en NVS, puis envoyé par SMS uniquement. Les transitions geofence produisent des D2E prioritaires et persistants.
8. En MQTT_TEST seulement, l'API s'abonne aux topics MQTT de télémétrie et de geofence, reçoit les JSON et les conserve en mémoire séparément.
9. Un dashboard ou un autre client consulte ensuite les données par HTTP ou WebSocket.

`DJUA_COMM_PROFILE=1` sélectionne SMS seul ; `=2` MQTT_TEST ; `=3` HTTP_TEST.
`ENABLE_HTTP_BACKEND` est dérivé du profil HTTP, sans envoi simultané ni fallback.

## 1. Firmware ESP32

Le point d'entrée est [`iot_box.ino`](iot_box.ino).

### Démarrage

La fonction `setup()` effectue les opérations suivantes dans cet ordre :

1. ouverture du moniteur série à 115200 bauds ;
2. affichage de l'identifiant du kit ;
3. initialisation de l'INA219 batterie, du solaire puis du DC (si configuré) ;
4. initialisation des entrées ADC1 du ZMPT101B et de l'ACS712-30A, puis lecture de leur calibration NVS ;
5. initialisation du GPS NEO-6M ;
6. initialisation de la machine d'état geofence ;
7. initialisation de l'horloge RTC DS1302 ;
8. SMS_PRODUCTION : ouverture du journal NVS D2, lecture du compteur et de la file ; aucun Wi-Fi. En test seulement : tentative de connexion Wi-Fi, délai maximal de 15 secondes ;
9. configuration du modem SMS si son matériel a été confirmé, ou du client MQTT en MQTT_TEST ;
10. démarrage des compteurs geofence et télémétrie.

L'identifiant actuellement configuré est `DJUA-KIN-000001`.

### Boucle principale

À chaque passage dans `loop()`, le firmware :

- lit les caractères disponibles sur l'UART du GPS afin d'alimenter TinyGPSPlus ;
- en production, avance le dialogue AT et la file SMS sans attente réseau bloquante ;
- en test, contrôle le Wi-Fi et tente une reconnexion toutes les 10 secondes s'il est coupé ;
- en MQTT_TEST seulement, contrôle MQTT et tente une reconnexion toutes les 10 secondes s'il est coupé ;
- vérifie localement la geofence toutes les 5 secondes avec une position récente ;
- attend que la cadence d'acquisition électrique de 10 secondes soit écoulée ;
- lit les INA219 batterie, solaire et charges DC ;
- acquiert pendant une seconde les formes d'onde tension et courant de la charge AC ;
- récupère la position GPS disponible ;
- lit la date et l'heure du DS1302 ;
- en production, intègre les énergies signées par trapèzes, puis clôt la période à l'échéance ; en test, construit et publie la télémétrie historique.

Si la mesure INA219 est invalide, le firmware affiche les lectures bus, shunt
et courant, tente une réinitialisation I²C, puis poursuit la mesure AC. La
télémétrie marque alors la batterie invalide au lieu de bloquer tout le cycle.

### Charges DC : troisième INA219

Le [pilote DC](src/sensors/dc_load_power_sensor.cpp) utilise un shunt externe
paramétré en courant nominal et chute nominale dans `config.h` (exemple 50 A/75 mV).
Il lit directement les registres INA219 avec sa calibration propre, sans utiliser
le calibre fixe des pilotes batterie/solaire. La tension est celle de VIN− côté charge.
Une absence/défaut éteint les flags DC ; le backend reçoit `null`, jamais un faux zéro.
L'énergie DC est intégrée par trapèzes avec les règles de complétude D2T2.
Voir [câblage et essais](TEST%20UNITAIRE/INA219_DC/README.md).

### Capteur de batterie INA219

Fichiers concernés :

- [`src/sensors/power_sensor.cpp`](src/sensors/power_sensor.cpp)
- [`src/sensors/power_sensor.h`](src/sensors/power_sensor.h)

Configuration actuelle :

| Élément | Valeur |
| --- | --- |
| Bus | I2C |
| Adresse INA219 | `0x40` |
| SDA ESP32 | GPIO 21 |
| SCL ESP32 | GPIO 22 |
| Horloge I²C | 100 kHz |
| Calibration | 32 V / 2 A |
| Filtrage | Médiane de 7 lectures |
| Plage batterie valide | 9,0 à 16,5 V |

Les valeurs suivantes sont réellement mesurées :

- tension totale en volts : tension du bus + tension du shunt ;
- courant en ampères ;
- puissance en watts : tension × courant.

Le moniteur série affiche aussi les trois grandeurs avant calcul. Une lecture
invalide déclenche une nouvelle initialisation du INA219 et une seconde série
de sept mesures. Aucun coefficient artificiel n'est appliqué à la tension bus.

### Capteur solaire INA219

Le driver indépendant `src/sensors/solar_power_sensor.cpp` utilise le bus partagé
SDA21/SCL22 et l'adresse cible `0x41`, à confirmer avec le scanner matériel.
Sept lectures filtrées donnent tension, courant signé et puissance ; l'énergie
d'intervalle est calculée uniquement si `solarMeasurementValid` est vrai.
Une mesure valide à 0 W ne déclenche aucune alerte. Trois cycles invalides donnent
WARNING, jamais CRITICAL à cause du solaire seul. La récupération efface cette
contribution ; les priorités geofence, les autres alertes et les patterns restent conservés.
`SOLAR_MEASUREMENT_ENABLED=0` désactive volontairement la mesure sans panne solaire.

Voir le [guide solaire](TEST%20UNITAIRE/INA219_SOLAR/README.md) pour le câblage,
les limites de calibration (shunt 0,1 Ω à vérifier), le scanner et les tests.

### Charge AC : ZMPT101B et ACS712-30A

Fichiers concernés :

- [`src/sensors/ac_power_sensor.cpp`](src/sensors/ac_power_sensor.cpp)
- [`src/sensors/ac_power_sensor.h`](src/sensors/ac_power_sensor.h)
- [`TEST UNITAIRE/ZMPT101B_ACS712`](TEST%20UNITAIRE/ZMPT101B_ACS712)

| Élément | Valeur |
| --- | --- |
| Tension | ZMPT101B sur GPIO34 (ADC1) |
| Courant | ACS712-30A, 66 mV/A nominal, sur GPIO35 (ADC1) |
| Protection ADC | Un diviseur 10 kohms / 10 kohms indépendant sur chaque sortie |
| Fenêtre | 1 seconde à période fixe de 300 µs, environ 3 333 paires |
| Persistance | Coefficients stockés dans la NVS ESP32 |

La tension et le courant RMS sont calculés après suppression de leur point
milieu. La puissance active n'est pas estimée par `Vrms × Irms` : le firmware
interpole les échantillons pour les remettre au même instant, applique la
correction de phase calibrée, puis calcule la moyenne de `v(t) × i(t)`. Il en
déduit séparément la puissance apparente et `PF = W / VA`. Pour une charge non
linéaire, ce PF réel inclut aussi la déformation des formes d'onde ; ce n'est
donc pas seulement le cosinus d'un déphasage fondamental.

La procédure complète de câblage et d'étalonnage est décrite dans le README du
test combiné. Les commandes série principales sont `AC CAL ZERO`,
`AC CAL VOLTAGE <V>`, `AC CAL CURRENT <A>` et `AC CAL PHASE_AUTO`.

### GPS NEO-6M

Fichiers concernés :

- [`src/sensors/gps.cpp`](src/sensors/gps.cpp)
- [`src/sensors/gps.h`](src/sensors/gps.h)

Configuration actuelle :

| Élément | Valeur |
| --- | --- |
| Port | UART matériel 2 |
| Vitesse | 9600 bauds |
| RX ESP32 | GPIO 16 |
| TX ESP32 | GPIO 17 |

TinyGPSPlus décode continuellement les données reçues. Une position est utilisable uniquement si elle est valide et si son âge ne dépasse pas `GPS_MAX_AGE_MS`, actuellement fixé à 5 secondes. Sinon, D2 envoie des champs absents `-` avec le flag GPS à zéro ; le JSON historique des profils de test conserve `0.0` pour les deux coordonnées.

Un ancien fix ne peut donc pas être utilisé comme position courante par la télémétrie ou le geofencing.

### Geofencing local et transmission selon le profil

La logique se trouve dans [`src/geofencing/geofence.cpp`](src/geofencing/geofence.cpp) et [`src/geofencing/geofence.h`](src/geofencing/geofence.h). La décision reste locale et continue donc à fonctionner sans réseau. Les transitions partent en D2E en production SMS ; les résultats détaillés sont publiés par [`src/communication/mqtt.cpp`](src/communication/mqtt.cpp) seulement en MQTT_TEST.

- zone circulaire centrée sur les coordonnées configurées ;
- rayons actuellement configurés : entrée 10 m, sortie 15 m (les JSON ci-dessous sont des exemples historiques, pas les constantes actives) ;
- vérification toutes les 5 secondes ;
- trois positions consécutives requises avant une transition ;
- états `UNKNOWN`, `INSIDE` et `OUTSIDE` ;
- événements uniques `GEOFENCE_EXIT` et `GEOFENCE_ENTER` lors des transitions ;
- conservation du dernier état confirmé lorsque le GPS est invalide ou trop ancien.

En MQTT_TEST, chaque contrôle, y compris lorsqu'aucune position GPS récente n'est disponible, est publié avec conservation sur `djua/test/DJUA-KIN-000001/geofence`. Les transitions sont en plus publiées sans conservation sur `djua/test/DJUA-KIN-000001/geofence/events`. Une transition qui coïncide avec une coupure MQTT est conservée en RAM et retentée après reconnexion ; elle ne survit pas à un redémarrage électrique. En SMS_PRODUCTION, seuls les événements confirmés partent en D2E et leur SMS complet est enregistré en NVS avant envoi ; aucun snapshot toutes les 5 secondes n'est envoyé par SMS.

Exemple de charge utile geofence :

```json
{
  "kit_id": "DJUA-KIN-000001",
  "timestamp": "2026-09-04T15:42:05+01:00",
  "timezone": "GMT+1",
  "state": "INSIDE",
  "event": "NONE",
  "position_usable": true,
  "latitude": -4.3251,
  "longitude": 15.3222,
  "distance_m": 0.0,
  "zone": {
    "center_latitude": -4.3251,
    "center_longitude": 15.3222,
    "enter_radius_m": 500.0,
    "exit_radius_m": 550.0
  },
  "confirmation": {
    "candidate_state": "UNKNOWN",
    "count": 0,
    "required": 3
  }
}
```

`event` prend la valeur `GEOFENCE_EXIT` ou `GEOFENCE_ENTER` lors d'une transition confirmée. Quand `position_usable` vaut `false`, les champs `latitude`, `longitude` et `distance_m` valent `null`, tandis que `state` conserve le dernier état confirmé. Le SMS D2E code ces événements `GX`/`GE` ; DJUA_SMS déduit le nouvel état.

### Horloge RTC DS1302

Fichiers concernés :

- [`src/sensors/rtc_ds1302.cpp`](src/sensors/rtc_ds1302.cpp)
- [`src/sensors/rtc_ds1302.h`](src/sensors/rtc_ds1302.h)

Configuration actuelle :

| Élément | Valeur |
| --- | --- |
| DAT / IO | GPIO 25 |
| CLK / SCLK | GPIO 26 |
| RST / CE | GPIO 27 |
| Fuseau fixe | `GMT+1` |

En production SMS, une date invalide n'est pas remplacée par la date de compilation. Les profils de test conservent cette initialisation automatique configurable. Si l'oscillateur est arrêté, le firmware le démarre. Une horloge déjà valide n'est pas réinitialisée.

Le timestamp JSON produit suit le format ISO 8601, par exemple `2026-09-04T15:42:05+01:00`. Si le DS1302 devient invalide ou indisponible, le firmware continue à fonctionner sans horodatage UTC inventé. En D2, RTC vaut `-` ; `UP` reste la durée de fonctionnement, jamais une date Unix.

### Champs de télémétrie

La structure complète est définie dans [`src/telemetry/telemetry.h`](src/telemetry/telemetry.h) et assemblée dans [`src/telemetry/telemetry.cpp`](src/telemetry/telemetry.cpp).

| Groupe | État actuel |
| --- | --- |
| Horodatage | DS1302 GMT+1 si valide ; UTC Unix sur D2, absent si invalide |
| GPS | Fix récent ; absence `-` en D2, `0.0` dans le JSON historique de test |
| Batterie | Mesure réelle du INA219 |
| Solaire | INA219 réel ; V, A, W et Wh ; champs réseau à `null` si mesure invalide ou désactivée |
| Charge AC | Mesure réelle ; télémétrie limitée à V, A, W, VA et Wh |
| Intervalle | Temps écoulé depuis le cycle précédent, en secondes |

Exemple de charge utile MQTT :

```json
{
  "kit_id": "DJUA-KIN-000001",
  "timestamp": "2026-09-04T15:42:05+01:00",
  "timezone": "GMT+1",
  "interval_seconds": 10,
  "latitude": 0.0,
  "longitude": 0.0,
  "battery": {
    "voltage_v": 12.4,
    "current_a": 0.42,
    "power_w": 5.21
  },
  "solar": {
    "voltage_v": 18.1,
    "current_a": 1.0,
    "power_w": 18.1,
    "energy_interval_wh": 0.050278
  },
  "ac_load": {
    "voltage_v": 230.1,
    "current_a": 1.25,
    "active_power_w": 244.5,
    "apparent_power_va": 287.6,
    "energy_interval_wh": 0.679
  }
}
```

Le champ `timestamp` contient exclusivement la date du DS1302 avec le décalage GMT configuré. Si le RTC est invalide, le message ne contient pas d'horodatage. `millis()` sert aux temporisations internes et à `UP` en D2 ; il ne remplace jamais une date UTC.

## 2. Communication réseau

### Wi-Fi

La gestion du Wi-Fi se trouve dans [`src/communication/internet.cpp`](src/communication/internet.cpp).

- Le mode utilisé est `WIFI_STA`.
- Le SSID et le mot de passe sont actuellement compilés dans le firmware via [`src/config.h`](src/config.h).
- La première tentative attend au maximum 15 secondes.
- En cas de coupure, une nouvelle tentative est déclenchée toutes les 10 secondes.

### MQTT dans le profil de test

La gestion MQTT se trouve dans [`src/communication/mqtt.cpp`](src/communication/mqtt.cpp).

Configuration utilisée seulement en MQTT_TEST :

| Élément | Valeur |
| --- | --- |
| Broker | `test.mosquitto.org` |
| Port | `1883` |
| Chiffrement TLS | Non |
| Authentification | Non |
| Topic télémétrie | `djua/test/DJUA-KIN-000001/telemetry` |
| Topic dernier contrôle geofence | `djua/test/DJUA-KIN-000001/geofence` |
| Topic événements geofence | `djua/test/DJUA-KIN-000001/geofence/events` |
| Topic état | `djua/test/DJUA-KIN-000001/status` |
| Client MQTT | PubSubClient |
| QoS de publication | 0 |
| File persistante | Aucune ; événements geofence conservés temporairement en RAM |

Lors d'une connexion réussie, l'ESP32 publie `online` sur le topic d'état avec conservation du message. Un Last Will MQTT publiera `offline` sur ce même topic si la connexion disparaît de manière anormale.

Les télémétries sont publiées sans conservation et avec le QoS par défaut de PubSubClient, soit QoS 0. Une télémétrie émise pendant une coupure reste perdue. Pour le geofencing, le dernier contrôle est retenu par le broker et une transition non envoyée est gardée temporairement en RAM jusqu'à la reconnexion MQTT.

### Backend HTTP optionnel

Le code sait aussi envoyer un `POST` JSON vers `BACKEND_URL`, avec la clé configurée dans l'en-tête `x-device-token`. Cette voie est compilée seulement si `ENABLE_HTTP_BACKEND` vaut `1`.

Elle vaut `1` uniquement en HTTP_TEST : un POST HTTP est effectué à chaque cycle de télémétrie, sans publication MQTT. Le timestamp provient du DS1302 lorsqu'il est valide ; sinon il est omis.

### GSM et SMS

Le codec D2, la séquence commune persistante par blocs de 256, la file de 16 SMS et le pilote AT non bloquant sont implémentés. D2T2 contient les mesures et énergies de période, dont les charges DC ; D2E contient une transition geofence confirmée. Le modem émetteur est un SIM800C sur une carte marquée « DC:5V-9V IN ». Le firmware principal utilise désormais `GSM_HARDWARE_CONFIGURED=1`, RX32/TX13, 9600 bauds et aucun PWRKEY attribué. Sans provisionnement explicite dans sa NVS `djua_d2`, aucun message D2 n'est créé. Le banc séparé a envoyé un vecteur D2T2 de séquence 5, reçu et authentifié dans l'API le 7 octobre 2026 ; après migration du banc de RX16/TX17 à RX32/TX13, l'utilisateur a également annoncé un essai réussi. Cela valide le banc avec des données simulées ; l'essai du firmware principal reste à effectuer. Le troisième INA219 absent reste non configuré et n'empêche pas l'envoi des autres groupes valides. L'acceptation `+CMGS`/`OK` seule n'est pas une confirmation de livraison à DJUA_SMS/backend. Voir [SMS D2T2](docs/SMS_D2T2.md) et [validation](docs/VALIDATION_D2T2.md).

## 3. API FastAPI

Le service est implémenté dans [`api/app/main.py`](api/app/main.py).

### Démarrage

Depuis le dossier `api` :

```powershell
uv sync
uv run uvicorn app.main:app --reload
```

L'interface Swagger est ensuite disponible à l'adresse <http://127.0.0.1:8000/docs>.

Au démarrage, l'API :

1. crée un client MQTT Paho ;
2. se connecte de manière asynchrone au broker ;
3. s'abonne par défaut à `djua/test/+/#` ;
4. accepte ainsi les télémétries, contrôles geofence et événements geofence de plusieurs identifiants de kit.

Le fichier `api/.env` n'est pas chargé automatiquement par l'application. Pour l'utiliser directement avec Uvicorn, il faut lancer :

```powershell
uv run uvicorn app.main:app --reload --env-file .env
```

Il est également possible de définir les variables dans le shell ou dans la configuration de déploiement.

### Traitement d'un message MQTT

Lorsqu'un message arrive, l'API :

1. décode le contenu en UTF-8 puis en JSON ;
2. ignore le message si le JSON est invalide ou si sa racine n'est pas un objet ;
3. utilise `kit_id` comme identifiant, ou l'extrait du topic si `kit_id` est absent ;
4. ajoute l'heure UTC de réception calculée par le serveur ;
5. ajoute un numéro de séquence interne ;
6. met à jour séparément la dernière télémétrie ou le dernier état geofence du kit ;
7. ajoute le message à l'historique global en mémoire avec son type (`telemetry`, `geofence` ou `geofence_event`).

L'accès à cet état partagé est protégé par un verrou entre le thread MQTT et les requêtes de l'API.

### Endpoints disponibles

| Interface | Fonction |
| --- | --- |
| `GET /health` | Retourne l'état général, l'état MQTT, le topic et le nombre de kits connus |
| `GET /devices` | Liste les kits ayant déjà transmis depuis le dernier démarrage |
| `GET /devices/{device_id}/telemetry/latest` | Retourne la dernière télémétrie du kit, ou 404 si elle n'existe pas |
| `GET /devices/{device_id}/telemetry?limit=100` | Retourne les derniers messages du kit |
| `GET /devices/{device_id}/geofence/latest` | Retourne le dernier état ou événement geofence du kit |
| `GET /devices/{device_id}/geofence?limit=100` | Retourne les contrôles et événements geofence récents du kit |
| `WS /ws/telemetry` | Envoie l'historique de télémétrie et les nouveaux messages, avec vérification toutes les 500 ms |
| `WS /ws/geofence` | Envoie l'historique geofence et les nouveaux contrôles/événements |

Le champ `status` de `/health` reste actuellement égal à `ok`, même si la sous-partie `mqtt.connected` vaut `false`. Il faut donc contrôler explicitement `mqtt.connected` pour connaître l'état de la liaison au broker.

### Stockage

Les messages ne sont pas enregistrés dans une base de données.

- L'historique est une file en mémoire limitée à 500 messages au total par défaut.
- La limite est globale à tous les appareils, pas 500 messages par appareil.
- Un redémarrage de l'API efface l'historique et la liste des appareils connus.
- La variable `TELEMETRY_HISTORY_LIMIT` permet de changer cette limite.

## 4. Configuration importante

### Firmware

Les constantes principales se trouvent dans [`src/config.h`](src/config.h) :

- identifiant du kit ;
- fuseau GMT et initialisation du DS1302 ;
- identifiants Wi-Fi ;
- adresse du backend HTTP et jeton associé ;
- broker, port et préfixe MQTT ;
- intervalles de télémétrie et de reconnexion.

### API

Les variables disponibles sont présentées dans [`api/.env.example`](api/.env.example) :

- `MQTT_BROKER_HOST` ;
- `MQTT_BROKER_PORT` ;
- `MQTT_TOPIC` ;
- `MQTT_USERNAME` ;
- `MQTT_PASSWORD` ;
- `TELEMETRY_HISTORY_LIMIT`.

## 5. Dépendances nécessaires

### Firmware Arduino/ESP32

Le code utilise les bibliothèques suivantes :

- framework Arduino pour ESP32 ;
- `Preferences` du framework ESP32 pour la calibration persistante ;
- `Adafruit INA219` ;
- `TinyGPSPlus` ;
- `Rtc by Makuna` pour le DS1302 ;
- `ArduinoJson` ;
- `PubSubClient` ;
- les bibliothèques ESP32 intégrées `WiFi`, `HTTPClient` et `Wire`.

Le dépôt ne contient actuellement ni `platformio.ini` ni liste Arduino formelle des versions. La reproductibilité de la compilation du firmware dépend donc des bibliothèques installées dans l'environnement Arduino utilisé.

### API

Les dépendances Python sont déclarées dans [`api/pyproject.toml`](api/pyproject.toml) et verrouillées dans `api/uv.lock` :

- FastAPI ;
- Paho MQTT ;
- Uvicorn avec ses dépendances standard.

Python 3.11 ou plus récent est requis.

## 6. Limites et précautions actuelles

Avant une utilisation en production, les points suivants doivent être traités :

- le broker MQTT est public, sans TLS ni authentification ;
- les topics publics peuvent être lus ou alimentés par des tiers ;
- les identifiants Wi-Fi et le jeton HTTP sont écrits dans `config.h` et doivent être considérés comme exposés s'ils ont été partagés ou versionnés ;
- l'API n'a aucune authentification ;
- CORS accepte actuellement toutes les origines ;
- l'API ne valide pas la structure métier détaillée du JSON reçu ;
- les messages ne sont pas persistés ;
- les données perdues pendant une coupure MQTT ne sont pas renvoyées ;
- le fuseau du DS1302 est fixe et ne gère pas automatiquement un changement saisonnier ;
- l'adresse, la polarité et les limites électriques du solaire restent à valider physiquement ; l'acceptation des valeurs solaires `null` par le POST HTTP externe reste à confirmer ;
- les coefficients AC livrés sont nominaux et exigent un étalonnage sur le matériel réel avant toute utilisation métrologique ;
- GSM/SMS D2 est implémenté et testé logiciellement, mais le modem émetteur, les vrais SMS avec le SIM868 et le backend D2 ne sont pas encore validés ; la file finie refuse explicitement de nouveaux messages en saturation ;
- les profils de test gardent leurs limites historiques de MQTT/HTTP ; la production SMS ne les active pas ;
- les clés HMAC en NVS ne sont pas automatiquement chiffrées, et HMAC ne chiffre pas les SMS.

Ces éléments décrivent l'état actuel ; ils ne signifient pas que le flux de test est inutilisable. Pour les essais, le chemin ESP32 → MQTT → FastAPI est déjà cohérent et opérationnel dès que l'ESP32 et l'API ont accès au broker.
