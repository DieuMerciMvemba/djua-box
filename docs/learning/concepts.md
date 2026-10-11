# Concepts structurants du projet

Ce document décrit seulement les concepts qui organisent réellement le système.
Il ne constitue ni une preuve de maîtrise ni une documentation ligne par ligne.

## Boucle principale embarquée

**Rencontré dans :** `iot_box.ino`.

**Pourquoi il existe ici :** l'ESP32 n'exécute pas un serveur classique. Après
une initialisation unique dans `setup()`, `loop()` répète la lecture du GPS, la
surveillance geofence, la maintenance réseau et les cycles de télémétrie.

**À retenir :** chaque opération longue retarde les opérations suivantes. Les
comparaisons avec `millis()` servent à planifier des actions sans multiplier les
temporisations bloquantes.

**Exemple du projet :** la geofence est contrôlée environ toutes les 5 secondes,
alors que la télémétrie complète est construite environ toutes les 10 secondes.

## Séparation en modules

**Rencontré dans :** `src/sensors`, `src/geofencing`, `src/telemetry`,
`src/communication`, `src/status` et `src/indicators`.

**Pourquoi il existe ici :** chaque dossier porte une responsabilité : acquérir,
décider, assembler, transporter ou afficher un état.

**À retenir :** `iot_box.ino` orchestre ces modules. Un pilote de capteur ne doit
pas connaître FastAPI, et l'API ne connaît pas le câblage des capteurs.

**Exemple du projet :** `readSolarPowerSensor()` produit un `SolarPowerData`,
`buildTelemetry()` l'intègre dans `TelemetryData`, puis les modules MQTT et HTTP
le sérialisent.

## Interfaces matérielles

**Rencontré dans :** `src/pins.h` et les pilotes de `src/sensors`.

**Pourquoi il existe ici :** les périphériques n'utilisent pas tous le même bus.
Les deux INA219 partagent I2C, le GPS utilise UART2, le DS1302 utilise ThreeWire
et les capteurs AC utilisent deux entrées ADC1.

**À retenir :** une adresse identifie un composant sur un bus partagé ; une
broche identifie une connexion physique. Une modification de configuration doit
rester cohérente avec le câblage et les tests.

**Exemple du projet :** les INA219 utilisent SDA 21 et SCL 22, avec deux adresses
distinctes définies dans `src/config.h`.

## Modèle de données et contrat JSON

**Rencontré dans :** `src/telemetry/telemetry.h`, `src/telemetry/telemetry.cpp`,
`src/communication/mqtt.cpp`, `src/communication/internet.cpp` et
`api/tests/test_mqtt_contract.py`.

**Pourquoi il existe ici :** les producteurs et consommateurs doivent donner le
même sens aux noms, types et unités des champs.

**À retenir :** `TelemetryData` est le modèle interne C++. Le JSON est le contrat
réseau. Les sérialisations MQTT et HTTP sont écrites séparément et peuvent donc
diverger si une seule est modifiée.

**Exemple du projet :** `solar.power_w` est une puissance instantanée, tandis que
`solar.energy_interval_wh` est calculée à partir de la puissance et de
`interval_seconds`.

## Validité, absence et valeur zéro

**Rencontré dans :** tous les pilotes de capteurs, `buildTelemetry()` et les
deux sérialisations réseau.

**Pourquoi il existe ici :** zéro peut être une vraie mesure. Par exemple, un
panneau connecté peut produire 0 W ; cela ne signifie pas que le capteur est
absent.

**À retenir :** une donnée doit transporter sa valeur et, lorsque nécessaire,
son état de validité. Le solaire invalide devient `null` sur le réseau. Dans le
code actuel, les mesures batterie et AC invalides sont initialisées à zéro et
leur drapeau interne n'est pas sérialisé, ce qui est une ambiguïté à connaître.

**Exemple du projet :** le test Python vérifie que `solar.power_w: null` reste
distinct de `solar.power_w: 0.0`.

## Horodatage et horloge monotone

**Rencontré dans :** `src/sensors/rtc_ds1302.cpp`, `src/telemetry`, MQTT, HTTP
et `api/app/main.py`.

**Pourquoi il existe ici :** le système a besoin à la fois d'une date réelle et
d'une mesure de durée robuste aux tours de boucle.

**À retenir :** le DS1302 fournit une date locale ISO 8601. `millis()` mesure le
temps écoulé depuis le démarrage ; ce n'est pas une date. L'API ajoute aussi
`received_at`, qui est l'heure UTC de réception par le serveur et non l'heure de
mesure.

**Exemple du projet :** un message contient `timestamp` seulement lorsque le
RTC est valide. L'enveloppe API ajoute `received_at`, qui reste une information
différente : la date de réception par le serveur.

## MQTT comme bus de messages

**Rencontré dans :** `src/communication/mqtt.cpp` et `api/app/main.py`.

**Pourquoi il existe ici :** l'ESP32 et l'API peuvent être sur des réseaux
différents tout en échangeant via un broker commun.

**À retenir :** le topic route le message. QoS 0 n'assure pas la livraison. Un
message `retained` permet à un nouvel abonné de recevoir le dernier état, mais
ne remplace pas un historique persistant.

**Exemple du projet :** la télémétrie est non retained ; le dernier état
geofence et l'état `online/offline` sont retained ; un événement geofence raté
est gardé temporairement dans la RAM de l'ESP32.

## HTTP, WebSocket et rôles des backends

**Rencontré dans :** `src/communication/internet.cpp` et `api/app/main.py`.

**Pourquoi il existe ici :** deux intégrations différentes coexistent. Le
firmware envoie un POST à un backend externe, tandis que l'API du dépôt écoute
MQTT puis offre des lectures HTTP et des flux WebSocket.

**À retenir :** HTTP sert ici aux requêtes ponctuelles ; WebSocket maintient une
connexion pour pousser des messages. Le backend du POST et l'API FastAPI locale
ne sont pas le même composant.

**Exemple du projet :** `GET /devices/{device_id}/telemetry/latest` lit le
dernier message reçu par MQTT ; aucun endpoint POST correspondant n'existe dans
`api/app/main.py`.

## Machine d'état et hystérésis

**Rencontré dans :** `src/geofencing/geofence.cpp`, `src/status/device_status.cpp`
et `src/indicators/indicators.cpp`.

**Pourquoi il existe ici :** une décision dépend de l'historique, pas seulement
de la dernière valeur. L'hystérésis évite d'alterner rapidement à la frontière
d'une zone.

**À retenir :** les rayons d'entrée et de sortie sont différents et plusieurs
positions consécutives confirment une transition. Les alertes ont ensuite une
priorité unique : geofence, critique, warning, aucune.

**Exemple du projet :** après un état `INSIDE`, trois positions au-delà du rayon
de sortie sont nécessaires pour produire `GEOFENCE_EXIT`.

## Calibration, filtrage et persistance NVS

**Rencontré dans :** `src/sensors/power_sensor.cpp`,
`src/sensors/solar_power_sensor.cpp` et `src/sensors/ac_power_sensor.cpp`.

**Pourquoi il existe ici :** les valeurs brutes dépendent du capteur, du shunt,
du diviseur, du bruit et du déphasage entre voies.

**À retenir :** une compilation correcte ne garantit pas une mesure juste. Les
INA219 filtrent plusieurs lectures par médiane. Les coefficients AC calibrés via
le moniteur série sont conservés dans la NVS de l'ESP32.

**Exemple du projet :** les commandes `AC CAL VOLTAGE`, `AC CAL CURRENT` et
`AC CAL PHASE_AUTO` modifient des coefficients utilisés lors des acquisitions
suivantes et après redémarrage.

## Stockage volatil et persistant

**Rencontré dans :** `api/app/main.py`, le tampon d'événement MQTT, le DS1302 et
la NVS utilisée par le capteur AC.

**Pourquoi il existe ici :** toutes les mémoires n'ont pas la même durée de vie.

**À retenir :** les dictionnaires et la `deque` de FastAPI disparaissent au
redémarrage. Le tampon geofence en RAM disparaît au redémarrage de l'ESP32. La
NVS conserve les calibrations et le DS1302 conserve l'heure grâce à son matériel.

**Exemple du projet :** `TELEMETRY_HISTORY_LIMIT` borne un historique global en
mémoire ; il ne configure aucune base de données.

## Concurrence et verrou

**Rencontré dans :** `api/app/main.py`.

**Pourquoi il existe ici :** Paho MQTT modifie les données depuis son thread,
pendant que FastAPI les lit depuis les requêtes HTTP ou WebSocket.

**À retenir :** `state_lock` évite qu'une lecture observe les collections au
milieu d'une modification. Le verrou protège la cohérence en mémoire, pas la
persistance ni l'authentification.

**Exemple du projet :** l'incrément de `message_sequence`, la mise à jour du
dernier message et l'ajout à `history` sont réalisés sous le même verrou.

## Authentification, autorisation et chiffrement

**Rencontré dans :** `src/config.h`, `src/communication/internet.cpp` et
`api/app/main.py`.

**Pourquoi il existe ici :** le Wi-Fi et le POST externe utilisent des secrets,
mais le chemin MQTT de test et l'API de consultation restent ouverts.

**À retenir :** authentifier un appareil, autoriser une action et chiffrer le
transport sont trois protections distinctes. Le système actuel n'est pas prêt
pour des données sensibles ou une exposition publique.

**Exemple du projet :** le POST ajoute `x-device-token`, mais utilise une URL
HTTP ; MQTT utilise le port 1883 sans identifiants ; FastAPI n'impose aucun
contrôle d'accès et autorise toutes les origines CORS.

## Contrat wire et HMAC D2

**Rencontré dans :** `src/communication/d2_codec.cpp` et `docs/SMS_D2.md`.

**Pourquoi il existe ici :** le boîtier et DJUA_SMS doivent interpréter exactement
les mêmes champs et vérifier que le SMS vient d'un appareil provisionné.

**À retenir :** les octets avant AUTH sont signés sans virgule finale. HMAC
authentifie le contenu, mais ne le chiffre pas ; une clé différente par appareil
doit rester hors Git. Les vecteurs publics ne sont pas des clés terrain.

**Exemple du projet :** le D2T maximal signé occupe exactement 160 septets.

## Idempotence et réservation durable des séquences

**Rencontré dans :** `src/communication/d2_store.cpp` et `sms_scheduler.cpp`.

**Pourquoi il existe ici :** une coupure pendant l'envoi peut rendre son résultat
incertain. Le retry ne doit ni changer les mesures ni devenir un nouvel événement.

**À retenir :** les blocs de 256 sont réservés en NVS avant utilisation. Le retry
garde le même SMS et donc le même message_id dérivé au récepteur. `+CMGS` n'est pas
un accusé de réception du backend ; une file finie peut saturer explicitement.

**Exemple du projet :** après réservation de 1..256 puis reboot, on repart à 257,
même si seules quelques séquences ont été consommées.

## Fenêtre énergétique et acquisition frontière

**Rencontré dans :** `src/telemetry/energy_window.h` et `iot_box.ino`.

**Pourquoi il existe ici :** la puissance varie pendant les 30 minutes entre
deux SMS ; la dernière puissance multipliée par 30 minutes serait trompeuse.

**À retenir :** chaque trapèze relie deux acquisitions valides espacées d'au plus
20 s. La frontière ferme A et initialise B. Une énergie incomplète est absente,
pas zéro ; un vrai flux inverse produit une énergie signée négative.

**Exemple du projet :** des acquisitions solaires invalides rendent `SE=-` pendant
la fenêtre concernée, même si une bonne mesure revient ensuite.

## Packing binaire et absence canonique D2T2

**Rencontré dans :** `src/communication/d2t2_codec.cpp`.

**Pourquoi il existe ici :** ajouter les mesures DC au SMS sans dépasser un
message GSM-7 exige un payload compact, compris exactement par DJUA_SMS.

**À retenir :** l'ordre, la largeur et le signe des champs font partie du contrat.
Un emplacement zéro avec flag éteint signifie absence ; avec flag allumé, il
signifie un vrai zéro mesuré. Base64URL est un encodage, pas du chiffrement.

**Exemple du projet :** le flag DC valide occupe le bit 7 ; l'énergie DC complète
occupe le bit 8. Les flags ont donc été élargis de 8 à 16 bits côté firmware.

## Shunt externe et calibration INA219

**Rencontré dans :** `src/sensors/dc_load_calibration.h` et `dc_load_power_sensor.cpp`.

**Pourquoi il existe ici :** le troisième INA219 mesure la petite chute de tension
du shunt placé en série avec les charges DC. La résistance réelle transforme
cette chute en courant ; la calibration configure les unités du registre courant.

**À retenir :** 50 A / 75 mV correspond à 1,5 mΩ. Modifier le shunt exige de
modifier sa configuration ; les limites du protocole ne sont pas celles du matériel.

**Exemple du projet :** `DC_LOAD_SHUNT_CONFIGURED=0` empêche toute mesure DC
tant que les valeurs exemples n'ont pas été validées sur le montage réel.
