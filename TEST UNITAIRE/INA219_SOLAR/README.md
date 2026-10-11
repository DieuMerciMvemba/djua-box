# INA219 solaire DJUA

Le deuxieme INA219 est un capteur officiel, active par defaut avec
`SOLAR_MEASUREMENT_ENABLED=1`. Son absence ne bloque pas le demarrage.
Ce dossier fournit un scanner, un sketch utilisant le driver de production
et des tests natifs. Aucun resultat physique n'est presume.

## Bus commun et adresses

| Signal | ESP32 | Batterie | Solaire |
| --- | --- | --- | --- |
| SDA | GPIO21 | SDA | SDA |
| SCL | GPIO22 | SCL | SCL |
| GND | GND | GND | GND |
| Adresse cible | | `0x40` | `0x41` |

La batterie initialise `Wire` a 100 kHz. Le solaire utilise
`solarIna219.begin(&Wire)`, sans `Wire.end()`, changement de broches ou
reinitialisation explicite du bus. Les sketches autonomes initialisent leur bus.
Le driver solaire ne touche jamais a l'adresse `0x40`.

Alimenter la logique avec une tension compatible ESP32 (3,3 V) et verifier
les resistances de tirage SDA/SCL : aucune remontee a 5 V vers l'ESP32.
Sur le breakout Adafruit, fermer A0 et laisser A1 ouvert selectionne `0x41`.
Pour un autre module, verifier son schema : ne pas transposer les ponts sans
controle. Configurer l'adresse hors tension.

1. Brancher la batterie seule, executer `i2c_scanner/i2c_scanner.ino` : attendre `0x40`.
2. Brancher le module solaire seul, deja configure : attendre `0x41`.
3. Brancher les deux sur le meme bus : attendre `0x40` **et** `0x41`.
4. Si seul `0x40` repond, le solaire n'est pas operationnel. Verifier adresse,
   alimentation, masses et cablage ; deux modules a `0x40` ne sont pas distinguables.

Un ACK confirme une adresse, pas l'identite du composant ni le circuit mesure.
Le scanner ne peut pas attribuer automatiquement le role batterie/solaire.

## VIN+, VIN-, polarite et limites

Couper les alimentations avant modification du circuit de puissance.
Pour mesurer le panneau avant le regulateur : positif panneau -> VIN+ solaire,
VIN- solaire -> entree positive du regulateur ; negatif panneau et reference GND
communs selon le schema de l'installation. Le shunt est **en serie** dans le
positif, jamais branche comme une charge entre les deux bornes du panneau.
L'INA219 ne remplace ni le regulateur de charge ni les protections du circuit.

La tension rapportee est `busVoltage + shuntVoltageMv / 1000` (VIN+ estime).
Le courant est `sensorCurrentMa / 1000 * SOLAR_CURRENT_POLARITY` ; la puissance
est `voltage * current`. Le signe reste conserve, y compris une puissance negative.
Comparer au multimetre et au sens reel panneau -> regulateur. Si le signe est
inverse pour le montage voulu, regler `SOLAR_CURRENT_POLARITY=-1`, sans `fabs`.

La calibration Adafruit `setCalibration_32V_2A()` suppose un shunt de **0,1 ohm**.
Verifier sa valeur, sa puissance admissible, les pistes et borniers du module,
la tension reelle du panneau (dont la tension a vide au froid) et son courant
maximal. Ces caracteristiques du panneau et du module ne sont pas connues ici.
Un shunt different exige une calibration adaptee avant exploitation des mesures.

Le nom `32V_2A` designe une configuration de registre, **pas une autorisation
d'appliquer 32 V** : la plage physique de mesure INA219 est 0 a 26 V.
Le logiciel rejette les tensions au-dela de 26 V, les chutes atteignant
±320 mV et les courants atteignant ±3,2 A pour cette calibration. Ces rejets ne
protegent pas electriquement le composant et ne garantissent pas le calibre du module.

References : [TI INA219](https://www.ti.com/product/INA219),
[adressage Adafruit](https://learn.adafruit.com/adafruit-ina219-current-sensor-breakout/pinouts),
[calibration Adafruit](https://github.com/adafruit/Adafruit_INA219/blob/master/Adafruit_INA219.cpp).

## Mesure et validite

Sept triplets tension bus / tension shunt / courant, espaces de 3 ms, sont
acquis. Chaque triplet doit avoir des lectures I2C reussies, des valeurs finies
dans les limites, une calibration presente et aucun bit OVF de saturation.
Au moins quatre triplets sont requis. Les medianes sont calculees independamment
(moyenne des deux valeurs centrales quand le nombre retenu est pair), puis la
tension et la puissance sont validees. Un outlier ne devient pas la mesure finale.

Une erreur lance au plus une reconfiguration du capteur solaire et une nouvelle
acquisition dans le cycle. Un capteur absent est recherche de nouveau au cycle
suivant. `sampleCount` decrit la derniere fenetre et `recoveryAttempted` la tentative.
`available` indique l'initialisation reussie, `valid` la qualite de la mesure.

- `solarMeasurementValid=true` avec 0 W : mesure valide, aucune panne solaire.
- `solarMeasurementValid=false` : mesure indisponible/invalide ; valeurs internes
  de telemetrie a NaN, aucune energie d'intervalle calculee.
- Mesure valide : `solarEnergyIntervalWh = solarPower * intervalSeconds / 3600`.
  C'est une estimation par puissance echantillonnee sur l'intervalle, pas un
  compteur cumulatif ni une integration continue du courant.

## Device status, LEDs et buzzer

`recordSolarStatus()` compte une fois par cycle final de mesure, apres recovery.
Les compteurs `solarFailures` et `solarUnavailable` sont independants de la batterie.
Dans `loop()`, le troisieme argument `true` differe le recalcul jusqu'au
`recordSensorStatus()` immediatement suivant. Toutes les mesures du cycle sont
ainsi prises en compte ensemble, sans niveau intermediaire ni bip artificiel
lors de recuperations simultanees. L'appel a deux arguments reste immediat.
Un ou deux echecs sont journalises sans WARNING ; a partir de
`SENSOR_WARNING_FAILURE_COUNT` (3 par defaut), contribution a WARNING uniquement.
Une mesure valide et un capteur disponible reinitialisent les compteurs.

Ordre conserve : GEOFENCE > CRITICAL > WARNING > NONE. Le solaire seul ne
produit jamais CRITICAL. Une autre anomalie peut maintenir la LED rouge apres
recuperation du solaire. Le buzzer existant est declenche seulement lors d'un
changement de niveau, jamais en continu ni a cause d'une production nulle.
Il est desactive par defaut (`BUZZER_ENABLED=0`), comme avant cette integration.
Les etats et timings SYSTEM, COMM et les patterns sonores ne sont pas modifies.

Avec `SOLAR_MEASUREMENT_ENABLED=0`, aucun acces I2C solaire ni WARNING solaire.
Le flag de validite reste faux et les valeurs reseau restent indisponibles.

## MQTT et HTTP

Les deux transports conservent exactement les quatre cles existantes, sans ajout
de `valid` ni de `energy_throughput_wh` :

```json
"solar": {
  "voltage_v": 18.1,
  "current_a": 1.0,
  "power_w": 18.1,
  "energy_interval_wh": 0.050278
}
```

Exemple pour 10 secondes. Une mesure techniquement invalide, ou le mode SOLAR=0,
donne ces memes quatre cles a `null`, jamais quatre zeros fictifs. Une vraie mesure
a 0 W conserve des nombres (dont 0 pour puissance et energie).

Le recepteur MQTT Python present dans `api/` conserve les `null`, y compris dans
sa lecture HTTP des dernieres donnees ; un test de contrat le verifie. Le serveur
de destination du POST `/api/iot/telemetry` est externe a ce depot. Son acceptation
des champs solaires nullables reste a verifier sur l'installation. Ne pas confondre
ce serveur avec l'API locale de consultation MQTT ; aucun POST reel n'a ete execute.

## Validation physique a effectuer

1. Confirmer les deux adresses avec le scanner et identifier chaque module seul.
2. Verifier panneau/shunt/calibre puis lancer `ina219_solar_test/ina219_solar_test.ino`
   a 115200 bauds. Il affiche adresse, Bus Voltage, Shunt Voltage, Solar Voltage,
   Solar Current, Solar Power, Samples, Recovery attempted et Valid.
3. Comparer tension et courant au multimetre, verifier la polarite et la puissance.
4. En absence de production, conserver le capteur alimente : `Valid=true`,
   puissance proche de 0 ; aucune alerte solaire dans le firmware complet.
5. Sur un banc adapte et en evitant tout court-circuit, simuler une perte du capteur
   solaire. Observer un/deux cycles invalides sans WARNING, puis WARNING au troisieme,
   sans CRITICAL solaire. Ne pas couper le bus commun de la batterie.
6. Si buzzer active, verifier un seul pattern a la transition. Reconnecter le module,
   attendre la prochaine acquisition : mesure valide, disparition du seul WARNING solaire.
7. Controler simultanement batterie, AC, GPS, RTC, geofence, heartbeat vert,
   LED communication, publications MQTT et reponses HTTP. Une panne physique qui
   maintient SDA/SCL a zero peut affecter les deux INA219 : isolation non garantie.
8. Verifier le POST HTTP externe avec production, 0 W valide et capteur absent ;
   confirmer les reponses 2xx et l'absence de conversion de `null` en zero en aval.

## Tests automatiques reproductibles

Depuis une invite developpeur Visual Studio :

```bat
"TEST UNITAIRE\INA219_SOLAR\host\run_tests.cmd"
"TEST UNITAIRE\INDICATORS\host\run_tests.cmd"
```

Le premier execute le driver de production avec un faux peripherique I2C,
la telemetrie, device_status et les vrais indicators. Quatre variantes : solaire
actif, polarite inversee, buzzer desactive, solaire desactive. Il teste aussi
NaN, hors plage, saturation, erreur I2C, calibration perdue, mediane, recuperation,
priorites, maintien des etats SYSTEM/COMM et non-repetition du buzzer.
Le formatage RTC seul est remplace par un stub dans ce test solaire.
Le second conserve les cinq variantes de tests indicators/geofence existantes.

```powershell
cd api
.\.venv\Scripts\python.exe -m unittest discover -s tests -v
```

Depuis la racine, avec `arduino-cli` dans le PATH (executer sequentiellement) :

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32 --warnings all --jobs 1 .
arduino-cli compile --fqbn esp32:esp32:esp32 --warnings all --jobs 1 --build-property 'compiler.cpp.extra_flags=-DSOLAR_MEASUREMENT_ENABLED=0' .
arduino-cli compile --fqbn esp32:esp32:esp32 --warnings all --jobs 1 'TEST UNITAIRE/INA219_SOLAR/i2c_scanner'
arduino-cli compile --fqbn esp32:esp32:esp32 --warnings all --jobs 1 'TEST UNITAIRE/INA219_SOLAR/ina219_solar_test'
git diff --check
```

Une compilation et des simulations reussies ne valident pas les adresses,
la polarite ou les limites electriques sur le materiel reel. Aucun flash automatique.

## Resultats automatiques du 22 septembre 2026

Cible `esp32:esp32:esp32`, sans televersement. Compilations finales reussies :

| Programme | Flash (octets) | RAM statique (octets) |
| --- | ---: | ---: |
| Firmware complet SOLAR=1 | 1 024 328 (78 %) | 100 248 (30 %) |
| Firmware complet SOLAR=0 | 1 022 524 (78 %) | 100 216 (30 %) |
| Scanner I2C | 293 876 | 23 500 |
| Test INA219 solaire | 301 788 | 23 620 |
| Sketch indicators existant, configuration par defaut | 273 432 | 22 148 |

Capacites annoncees : Flash programme 1 310 720 octets, RAM 327 680 octets.
Le surcout SOLAR=1 contre SOLAR=0 est de 1 804 octets de Flash et 32 octets
de RAM statique. Ces chiffres ne mesurent pas le pic de pile ou de heap en execution.

- Quatre variantes natives solaires : reussies, dont polarite -1, SOLAR=0,
  buzzer active/desactive, recuperations simultanees et maintien d'un WARNING
  quand une autre anomalie prend le relais sans nouveau bip.
- Cinq variantes natives indicators existantes : reussies (buzzer desactive,
  actif/passif et polarites haute/basse), avec les tests geofence existants.
- Quatre tests Python du contrat MQTT : reussis, dont la conservation de `null`
  distinct de 0 et la consultation HTTP locale des donnees recues.
- `git diff --check` : reussi.
- Drivers batterie, AC, GPS, RTC, geofence, indicators et broches : aucune
  modification de leur code, verification par diff.

Les avertissements preexistants ArduinoJson, formatage RTC, TinyGPSPlus et
Adafruit_INA219 restent presents ; aucun avertissement dans le nouveau driver solaire.
Aucune validation physique de l'adresse, du panneau, du shunt ou de la polarite,
aucune publication MQTT/POST HTTP reel de test, aucun commit ni push.
