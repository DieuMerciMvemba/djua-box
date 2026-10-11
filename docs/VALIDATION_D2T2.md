# Vérification D2T2 / INA219 DC — 7 octobre 2026

## Résultats automatiques exécutés

| Vérification | Résultat |
| --- | --- |
| Baseline avant modification : tests SMS D2 | 4 tests PASS, 6 groupes C++ PASS |
| Tests SMS après intégration | 7 tests PASS, 8 groupes C++ PASS |
| Vecteurs officiels D2T2 | 10 vecteurs vérifiés ; 3 messages valides reproduits exactement, 7 cas de validation/binding |
| Vecteurs D2 historiques / D2E | 14 vecteurs vérifiés ; encodage historique conservé |
| Packer D2T2 | 60 octets identiques, Base64URL/signed_part/HMAC/SMS final identiques |
| Oracle réel DJUA_SMS en lecture seule | 7 messages C++ acceptés : 3 D2T2 et 4 D2E ; JSON attendus identiques |
| Pilote DC et shunt externe | 4 variantes PASS : configuré, polarité inversée, non configuré, désactivé |
| Import USB clé du banc | 12 tests PASS, port simulé seulement |
| Régression solaire | 4 variantes PASS |
| Régression LED/buzzer/statuts | 7 variantes PASS |
| `git diff --check` | Pas d'erreur de contenu ; avertissements habituels LF/CRLF sur Windows |

Le binding expéditeur/appareil des vecteurs de refus est contrôlé par la passerelle,
pas par le parseur émetteur qui ne connaît pas le numéro attribué par l'opérateur.
Les tests C++ valident donc le wire/HMAC de ce vecteur comme valide et le test
de réception de production reste nécessaire pour vérifier le binding réel.

La copie D2T2 est identique à la source locale : SHA256
`7A6CA9DE6874AC2B837BC172BD95B2E36A5AAADCD1D6DC705EB74B580A4B7FA4`.
Les vecteurs D2 historiques sont aussi identiques sémantiquement à la source lue.
Référence HEAD DJUA_SMS : `c5835077b2b6e8418e34898013cacc0892c12e25`.

## Scénarios couverts

- Nombres signés, arrondi aux demi-unités, vrais zéros et absence canonique.
- Flags réservés, bits finaux réservés, payload trop court, dépendance des flags,
  champs hors plage, HMAC absent en production ou altéré même en développement.
- Réservation des séquences, reboot, compteur manquant/corrompu, épuisement uint32,
  interruption des écritures, file pleine et conservation des octets après coupure.
- Migration d'une file contenant un ancien D2T : restauration inchangée, nouveaux
  messages D2T2 puis D2E dans le même compteur.
- Timeout et reprise d'un SMS D2T2, priorité D2E, backoff, CPIN/CREG/CMGS simulés.
- Attente alimentation, impulsion PWRKEY puis attente boot, passage de millis à zéro.
- Calibration externe 50 A/75 mV, lecture des registres DC, médiane, flux inverse,
  overflow, registre non prêt, saturation, calibration perdue, défaut I²C, récupération.
- WARNING DC après trois pannes, récupération et priorité de l'alerte geofence.
- Trapèzes d'énergie signée, trous >20 s, acquisition invalide, absence d'extrapolation.

## Compilations ESP32

Les chiffres ci-dessous proviennent de compilations exécutées, sans téléversement.
La configuration UART/PWRKEY/shunt de compilation est une fixture, pas une
validation du câblage réel.

| Cible | Résultat | Flash (octets) | Variables globales (octets) |
| --- | --- | ---: | ---: |
| SMS_PRODUCTION, défaut | PASS | 392247 | 77512 |
| MQTT_TEST | PASS | 994955 | 99452 |
| HTTP_TEST | PASS | 1023779 | 99876 |
| SMS, trois INA actifs + UART + provisionnement + PWRKEY (compilation seulement) | PASS | 386735 | 77744 |
| SMS, mesure DC désactivée | PASS | 392247 | 77512 |
| Banc ESP32 D2T2/HMAC | PASS | 306940 | 25820 |
| Sketch INA219 DC, non configuré par défaut | PASS | 293972 | 23500 |
| Scanner I²C trois adresses | PASS | 294020 | 23500 |

Les cinq profils du lanceur ont terminé avec succès. Le profil complet a été
recompilé après correction du placement de la broche PWRKEY dans la copie de test
(définition dans pins.h, afin d'éviter une redéfinition selon l'ordre des inclusions).
Des avertissements concernent TinyGPSPlus, Adafruit_INA219, l'API ArduinoJson
historique et un tampon de diagnostic RTC préexistant ; ce travail ne migre pas
ces bibliothèques et ne réécrit pas le pilote RTC.

## Matériel et réception

**Lors des vérifications automatiques initiales :** aucun téléversement sur ESP32,
autotest mbedTLS sur la carte, mesure du nouveau shunt ni émission SMS réelle
n'avait été exécuté. L'essai manuel communiqué ensuite est décrit ci-dessous.

Détection Arduino autorisée exécutée : COM3, COM4, COM6 et COM16, cartes inconnues.
Aucun port émetteur n'a été choisi automatiquement. Aucun service récepteur,
configuration de clé, base SQLite ou fichier de DJUA_SMS n'a été modifié.

L'utilisateur a indiqué SIM800C, carte prévue pour 5 V (alimentation à dimensionner),
shunt externe non choisi et adresse INA219 non réglée. Les paramètres exemples
restent non activés pour le matériel. Le prompt rapporte une réception D2T2 déjà
réussie sur la chaîne passerelle/backend ; cela ne prouve pas l'envoi par ce firmware.
L'exemple SMS documenté est le vecteur public reproduit sur PC, pas une émission réelle.

Les photos reçues le 7 octobre 2026 montrent une carte SIM800C V1.34, dos marqué
« DC:5V-9V IN » : la compatibilité annoncée de son entrée avec le 5 V est donc
établie par le marquage. Le dimensionnement de l'alimentation, les niveaux UART,
le rôle de VMCU et le démarrage restent à valider. Cette inspection visuelle
n'ajoute aucun essai électrique ni envoi SMS aux résultats ci-dessus.

### Essai manuel du banc communiqué par l'utilisateur

Le script d'import USB sur COM20 a répondu `KEY_OK: test key
provisioned/confirmed on ESP32; no SMS sent.` Puis le journal série de l'ESP32
a montré l'exécution de `SEND VECTOR` avec les résultats suivants :

- Message `D2T2` pour `DJUA-TEST-20261006-A`, séquence 5.
- Marqueur `SYNTHETIC_VECTOR` : données simulées, aucune mesure physique DC.
- SIM800C : `+CMGS: 12`, puis `OK`.
- Banc : `MODEM_ACCEPTED sequence=5 queue_commit=OK`.

Ce journal fourni par l'utilisateur établit l'acceptation du SMS par le modem
et l'enregistrement local de ce résultat. L'agent n'a pas effectué lui-même
ce téléversement/envoi.

### Réception API confirmée par la réponse fournie par l'utilisateur

La réponse de télémétrie communiquée pour ce même essai contient :

- `device_id` et `kit_id` : `DJUA-TEST-20261006-A`.
- `protocol=D2T2`, `auth_status=VERIFIED`, `data.sequence=5`.
- `message_id=D2:DJUA-TEST-20261006-A:5`.
- Réception passerelle : `2026-10-07T16:12:25.282519+00:00`.
- Réception API : `2026-10-07T16:12:25.600681+00:00`.
- DC : 12,35 V, 3,2 A, 39,5 W, 19,76 Wh ; flags DC et énergie DC valides.
- Données AC, batterie, solaire et GPS également présentes conformément à
  la fixture `SEND VECTOR`.

La chaîne ESP32 → SIM800C → passerelle → MQTT → API est donc confirmée par
les journaux/réponses transmis pour ce message de test. Le contrôle de signature
est confirmé par `VERIFIED` ; cette réponse ne constitue pas un test négatif du
binding expéditeur/appareil. Le compteur externe `sequence=3` est le compteur
local de réception de l'API, distinct du compteur SMS `data.sequence=5`.
L'heure `timestamp=2026-09-27T16:30:00+01:00` appartient au vecteur simulé ;
les dates de réception ci-dessus sont les dates à utiliser pour identifier cet essai.
Les flags valides décrivent le vecteur envoyé, pas des acquisitions physiques.

Restent à valider : câblage GSM final GPIO32/13, troisième INA219 et shunt réel,
puis acquisitions et envoi avec le firmware principal. Le premier essai décrit
ci-dessus utilisait GPIO16/17. L'utilisateur a ensuite réglé le banc sur RX32/TX13 ;
son message de démarrage et son guide ont été alignés. L'utilisateur a ensuite
indiqué « C EST BON » ; le succès du nouvel essai sur RX32/TX13 est donc rapporté,
sans nouveau journal ni JSON pour en consigner la séquence. L'identité/NVS du
banc reste dédiée.

### Activation du GSM dans le firmware principal sans troisième INA physique

L'utilisateur a précisé ne pas encore disposer du troisième INA219. Le GSM
peut fonctionner indépendamment : `src/pins.h` définit maintenant RX32/TX13,
`src/config.h` active `GSM_HARDWARE_CONFIGURED=1`, baud 9600, et PWRKEY reste
non attribué. Le journal de démarrage affiche les broches effectives.
`DC_LOAD_SHUNT_CONFIGURED=0` est conservé : aucun accès I²C au capteur DC,
aucune alerte de panne DC liée à son absence, aucune mesure DC inventée.

Les tests SMS ont été réexécutés : 7 tests Python et 8 groupes C++ PASS.
Les quatre variantes du pilote DC (configuré, inversé, non configuré, désactivé)
ont également été réexécutées : PASS. Le profil `SMS_PRODUCTION` a ensuite été
recompilé avec le GSM activé et le DC non configuré : PASS, 392431 octets de
flash et 77512 octets de variables globales. Les avertissements RTC, TinyGPSPlus
et Adafruit_INA219 préexistants restent présents. `git diff --check` : PASS.
Le provisionnement NVS `djua_d2` et
l'essai avec `iot_box.ino` restent à effectuer ; la clé du banc ne les remplace
pas. Aucune clé n'a été copiée ni aucun téléversement réalisé par l'agent.

## État Git

Les modifications préexistantes ont été conservées et complétées sur les composants
concernés. Les changements déjà présents dans `api/tests/test_mqtt_contract.py`,
`src/sensors/power_sensor.cpp`, `src/sensors/rtc_ds1302.cpp` et les anciens lanceurs
de tests solaire/indicateurs n'ont pas été réécrits par cette intégration.
Aucun commit, push, fusion ou effacement de NVS n'a été effectué.
