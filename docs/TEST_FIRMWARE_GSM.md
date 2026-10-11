# Test du firmware principal : GSM / D2T2, sans troisième INA219

Ce guide teste `iot_box.ino` avec les capteurs disponibles et le SIM800C sur
RX32/TX13. Le troisième INA219 n'est pas encore disponible. Aucun champ de
mesure n'est simulé : une acquisition absente/invalide est déclarée absente.
Le banc `d2_esp32_hmac_test` reste un programme distinct.

## 1. Conserver le câblage GSM validé

| ESP32 | SIM800C |
| --- | --- |
| GPIO32, réception UART1 | TXD |
| GPIO13, émission UART1 | RXD |
| GND | GND |

Reprendre l'alimentation 5 V et le montage VMCU déconnectée déjà utilisés par
l'utilisateur. Le modem doit être allumé ; aucun GPIO PWRKEY n'est attribué.
Le GPS reste sur GPIO16/17. Les INA219 existants partagent SDA21/SCL22.
Ne pas raccorder les GPIO de mesure AC directement à une sortie 5 V : conserver
le montage d'adaptation existant.

## 2. Vérifier la configuration du programme principal

Dans `src/config.h`, conserver :

```cpp
#define DJUA_COMM_PROFILE DJUA_PROFILE_SMS_PRODUCTION
#define GSM_HARDWARE_CONFIGURED 1
#define GSM_BAUD_RATE 9600UL
#define DC_LOAD_SHUNT_CONFIGURED 0
```

Le solaire est actuellement désactivé (`SOLAR_MEASUREMENT_ENABLED=0`). Ne
l'activer que si son adresse, son montage et sa mesure ont déjà été validés.
Dans `src/pins.h`, GSM RX=32, TX=13, PWRKEY=-1 ; GPS RX=16, TX=17.

Pour raccourcir cet essai, remplacer temporairement la valeur existante :

```cpp
#define D2_PERIOD_SECONDS 60UL
```

Ne pas ajouter une seconde définition. Cette période est celle de la fenêtre
d'acquisition ; l'envoi peut commencer au cycle de mesure suivant la minute,
puis prendre le temps nécessaire à l'initialisation/soumission du modem.
Après provisionnement, laisser le programme tourner sans le redémarrer à chaque
minute. Les D2E confirmés peuvent aussi être transmis pendant le test.

## 3. Vérifier l'identité destinée à ce firmware

L'utilisateur a confirmé que `DJUA-KIN-000001` est déjà enregistré dans la
passerelle avec sa clé et le numéro de SIM émettrice. Utiliser cette identité
pour cet essai, avec sa clé existante et son compteur autorisé.

Le journal du programme principal utilise la NVS `djua_d2`, tandis que le banc
utilise `djua_d2_bench`. Le `KEY_OK` du banc ne provisionne pas `iot_box.ino`.
Le DEV utilisé dans les SMS est l'identité provisionnée en NVS, pas simplement
la constante `DEVICE_ID`.

Avant de provisionner, disposer de l'identité autorisée dans la passerelle,
de sa clé privée de 64 caractères hexadécimaux, du numéro E.164 du récepteur
et de la séquence initiale autorisée. L'association expéditeur/appareil concerne
le numéro de la SIM **émettrice** ; la destination NVS est le numéro de la SIM
**réceptrice**. Ne pas réutiliser l'identité du banc avec un compteur remis à 1.
Si ces informations ne sont pas connues, terminer seulement le diagnostic du
démarrage, puis les préciser avant le provisionnement. Aucun fichier de
DJUA_SMS n'est modifié par ce guide ni par le firmware.

## 4. Téléverser le firmware principal

Ouvrir `C:\Users\Dell\Downloads\iot_box\iot_box.ino` dans Arduino IDE.
Sélectionner ESP32 Dev Module et le port USB réel de l'ESP32 (COM20 lors du
dernier essai). Désactiver l'effacement complet de la flash/NVS puis téléverser.
Cela remplace le programme de banc sans effacer volontairement ses données.

Ouvrir le moniteur série à **115200 bauds**, fin de ligne **Nouvelle ligne**.
Vérifier :

```text
[GSM] UART1 configure | module=SIM800C | RX=32 | TX=13 | baud=9600
```

Le démarrage indique aussi `[D2] reference=... | etat=...` :

- `D2_READY` : identité/clé/compteur déjà provisionnés ; sauter l'étape 5.
- `D2_UNPROVISIONED` : aucune nouvelle création D2 autorisée ; suivre l'étape 5.
- Défaut compteur/stockage : conserver le diagnostic et arrêter le test de
  provisionnement ; ne pas effacer la NVS pour contourner le défaut.

Le firmware principal ne reproduit pas nécessairement les réponses AT brutes
du banc. Ne pas attendre les lignes `[BENCH] MODEM_READY` ou `SELFTEST_OK`.

## 5. Provisionner uniquement si l'état est D2_UNPROVISIONED

Dans `src/config.h`, mettre temporairement `D2_PROVISIONING_ENABLED=1`,
téléverser à nouveau sans effacement, puis ouvrir le moniteur série.
La console de calibration AC est suspendue pendant ce mode d'installation.

Taper d'abord :

```text
D2 STATUS
```

Attendre `D2_UNPROVISIONED`. Préparer ensuite, **localement et sans la partager**,
la commande dont voici seulement le modèle :

```text
D2 PROVISION DJUA-KIN-000001 <CLE_PRIVEE_64_HEX> <NUMERO_RECEPTEUR_E164> installation-gsm <SEQUENCE_INITIALE_AUTORISEE>
```

Les chevrons sont des emplacements à remplacer, pas des caractères à envoyer.
Une identité entièrement nouvelle et jamais utilisée peut commencer à 1 ;
une identité ayant un historique nécessite de préserver son compteur autorisé.
Une identité perdue après destruction de la NVS ne doit pas être recréée en
réinitialisant simplement son compteur.

Attendre :

```text
[D2] Provisionnement durable OK
```

Puis `D2 STATUS` doit montrer `D2_READY`. Le programme commence alors à créer
et envoyer automatiquement les messages selon sa période. En cas de refus,
ne pas effacer la NVS ni modifier la clé de la passerelle pour le contourner.
La clé privée ne doit pas apparaître dans une capture, un journal partagé ou
le dépôt. Ne pas utiliser `provision_bench.py`, réservé au firmware de banc.

Remettre `D2_PROVISIONING_ENABLED=0` et téléverser sans effacement avant de
poursuivre le test normal. En mode normal, `D2 STATUS` n'est plus disponible ;
l'état du journal est toujours affiché au démarrage.

## 6. Préparer la réception et observer l'envoi automatique

Laisser la passerelle et le broker MQTT habituels fonctionner. Si l'API est
arrêtée, ouvrir un autre PowerShell :

```powershell
Set-Location 'C:\Users\Dell\Downloads\iot_box\api'
uv run uvicorn app.main:app --reload --host 127.0.0.1 --port 8000
```

Laisser le firmware fonctionner au-delà d'une minute d'acquisition. Observer :

```text
[D2] PERIODIQUE durablement enregistre | file=...
[SMS] Accepte par modem/operateur ; reception gateway/backend NON CONFIRMEE
```

Il n'y a pas de commandes `SEND`, `SEND VECTOR` ou `RETRY` dans ce firmware.
Les reprises sont automatiques, avec le même SMS conservé en NVS. Un refus
`MESSAGE NON CREE`, un défaut SIM/réseau ou un timeout doit être diagnostiqué
à partir du journal, sans réinitialiser le compteur.

## 7. Vérifier l'API avec l'identité provisionnée

Consulter `/devices/<IDENTITE_NVS>/telemetry/latest` sur `http://127.0.0.1:8000`.
Si l'identité autorisée/provisionnée est `DJUA-KIN-000001`, l'URL est :

<http://127.0.0.1:8000/devices/DJUA-KIN-000001/telemetry/latest>

Vérifier le nouvel identifiant de message et les nouvelles dates de réception :

- `data.protocol=D2T2`, `data.auth_status=VERIFIED`.
- `data.kit_id` et `device_id` égaux à l'identité NVS.
- `data.interval_seconds` proche de 60 pendant ce test, selon les cycles réels.
- Valeurs batterie/AC/GPS/RTC présentes seulement lorsque leur acquisition est
  valide ; ne pas attendre les valeurs fixes de `SEND VECTOR`.
- Valeurs DC `null`, `validity.dc_load=false`, `validity.dc_load_energy=false`.
- Solaire absent tant que sa fonction est désactivée.

La séquence SMS est `data.sequence` ; le compteur externe `sequence` appartient
à l'API. Une séquence élevée après redémarrage peut venir de la réservation par
blocs de 256. La réception D2T2 et `VERIFIED` confirment la chaîne de transport ;
la précision des mesures nécessite l'étalonnage des capteurs.

## 8. Restaurer les réglages normaux

Après validation, remettre `D2_PERIOD_SECONDS=1800UL` (30 minutes) et conserver
`D2_PROVISIONING_ENABLED=0`, `GSM_HARDWARE_CONFIGURED=1`,
`DC_LOAD_SHUNT_CONFIGURED=0`. Téléverser sans effacer la NVS.

Conserver comme preuve les journaux d'envoi sans clé, le JSON API et l'identité/
séquence/heure du message. Le troisième INA219 pourra être monté et validé
ensuite ; son absence ne bloque pas ce test GSM.
