# Intégration firmware D2T2 et charges DC — 7 octobre 2026

## Contrat et périmètre

DJUA_SMS est consulté **strictement en lecture seule**. Référence locale HEAD :
`c5835077b2b6e8418e34898013cacc0892c12e25`.
Les contrats lus sont `docs/sms_protocol_d2t2.md` et `docs/sms_protocol_v2.md`,
avec leurs vecteurs officiels. Aucun parser, fichier, clé ou réglage du récepteur
n'a été modifié pour cette intégration.

Les nouveaux messages périodiques sont D2T2 ; les événements urgents restent D2E.
Le décodeur historique D2T est conservé pour relire les SMS immuables déjà stockés
avant migration et pour les tests de conformité. Un ancien D2T déjà en file peut
donc encore être retransmis tel quel ; aucun nouveau D2T n'est mis en file.

## Architecture

```text
INA219 batterie 0x40 ─┐
INA219 solaire 0x41 ──┤
INA219 DC 0x44* ──────┼→ ESP32 : acquisitions toutes les 10 s
ZMPT101B + ACS712 ────┘       → fenêtres énergie solaire/AC/DC
RTC + GPS ───────────────────→ D2T2 toutes les 1800 s, période réelle
GPS → geofence ──────────────→ D2E GX/GE dès transition confirmée
                              → compteur commun + HMAC + SMS complet en NVS
                              → scheduler / retries des mêmes octets
                              → UART1 / SIM800C / GSM-7
                              → SIM868 / DJUA_SMS → MQTT → backend
```

`* 0x44` est une adresse proposée, encore à configurer physiquement.
La batterie initialise Wire ; le pilote DC ne change ni les broches ni l'horloge
du bus. Les profils MQTT/HTTP restent exclusifs et proposent aussi `dc_load`,
avec des valeurs `null` en cas d'absence. Leur énergie DC utilise des trapèzes ;
les énergies solaire/AC historiques de ces profils restent des estimations.

## Mesures et flags

| Donnée réelle | Champs D2T2 | Présence |
| --- | --- | --- |
| DS1302 converti GMT+1 → UTC | RTC | bit 0 |
| NEO-6M récent | LAT/LON | bit 1 |
| INA219 batterie | BV/BI/BP | bit 2 |
| INA219 solaire | SV/SI/SP | bit 3 |
| ZMPT101B + ACS712 | AV/AI/AP/AS | bit 4 |
| Intégration solaire | SE | bit 5, exige bit 3 |
| Intégration AC | AE | bit 6, exige bit 4 |
| INA219 DC + shunt externe | DV/DI/DP | bit 7 |
| Intégration DC | DE | bit 8, exige bit 7 |

FLAGS est désormais un `uint16_t`. Les bits 9..15 et les six bits finaux sont zéro.
Pour chaque groupe absent, les emplacements binaires sont zéro et le flag est
éteint : le récepteur restitue `null`. Un vrai zéro mesuré conserve son flag allumé.
Le pilote DC mesure côté charge et respecte la polarité configurable ; consommation
positive, retour négatif. La configuration du shunt externe est décrite dans
[le guide INA219 DC](../TEST%20UNITAIRE/INA219_DC/README.md).

Les énergies sont intégrées par trapèzes entre deux mesures valides espacées
d'au plus 20 s. Une mesure invalide ou un trou supérieur rend l'énergie absente
jusqu'à la fin de la période. L'acquisition frontière termine A et démarre B,
sans compter deux fois le même segment. Les retries ne changent pas cette période.

## Encodage et authentification

`d2t2_codec.cpp` écrit exactement 480 bits, MSB en premier, avec complément à deux
pour les nombres signés et arrondi à mi-chemin éloigné de zéro. Le résultat est
60 octets puis 80 caractères Base64URL sans `=`.

SMS : `D2T2,DEV,PAYLOAD,AUTH`. La chaîne ASCII exacte `D2T2,DEV,PAYLOAD` est signée
par HMAC-SHA256, clé appareil de 32 octets. AUTH encode les huit premiers octets
du digest en onze caractères Base64URL. Aucune virgule finale dans la signature.
Un DEV de 32 caractères donne au maximum 130 septets GSM-7, dans un SMS unique.
L'authentification et le contrôle des entrées ne sont pas assouplis.

## Compteur, migration et retries

Le format NVS des enregistrements `identity`, `sequence` et `queue`, ainsi que le
namespace `djua_d2`, restent inchangés. Le journal réserve des blocs de 256 avant
emploi. D2T2 et D2E partagent le compteur uint32 ; les anciens numéros D2T ne
sont pas réutilisés. Un reboot peut laisser des trous, jamais un retour à 1.
Après UINT32_MAX : arrêt explicite de création, sans wrap à zéro.

La file conserve le texte signé avant transmission : tous les retries renvoient
ce texte, avec le même numéro, la même heure, les mêmes mesures et le même HMAC.
Elle contient au plus 16 messages, dont quatre périodiques, et refuse explicitement
la saturation. Les urgences éligibles sont prioritaires. Backoff 30 s → 15 min.
L'acceptation `+CMGS` puis `OK` permet le retrait durable ; elle ne prouve pas
la réception par DJUA_SMS ou le backend.

## SIM800C et démarrage

Procédure pas à pas : [test du firmware principal sans troisième INA219](TEST_FIRMWARE_GSM.md).

UART1 dédié, 9600 bauds, `GSM_RX_PIN=32`, `GSM_TX_PIN=13` ; GPS conservé sur
UART2 GPIO16/17. L'utilisateur a confirmé le fonctionnement du banc après son
passage à RX32/TX13. Ce câblage est désormais repris dans le firmware principal
avec `GSM_HARDWARE_CONFIGURED=1` : TXD modem vers GPIO32, RXD modem vers GPIO13,
masse commune. Le dimensionnement de l'alimentation reste à vérifier.

Le troisième INA219 n'est pas nécessaire au fonctionnement du GSM. En son absence,
garder `DC_LOAD_SHUNT_CONFIGURED=0` : aucun accès I²C DC, flags DC éteints et
mesures DC absentes dans D2T2. Les autres groupes valides peuvent être transmis.

**Mise en service du firmware principal :** ouvrir et téléverser `iot_box.ino`
sans effacer la flash/NVS. Son identité, sa clé et sa file sont dans `djua_d2`,
distinct de `djua_d2_bench` : le `KEY_OK` du banc ne provisionne pas le firmware
principal. Si le démarrage indique `D2_UNPROVISIONED`, compiler temporairement
avec `D2_PROVISIONING_ENABLED=1` puis suivre le
[provisionnement USB](SMS_D2.md#provisionnement-local-supervise).
Employer l'identité, la clé privée, le numéro récepteur et le prochain compteur
autorisés pour cet appareil dans la passerelle ; ne pas recopier l'identité du
banc avec un compteur réinitialisé. Le script `provision_bench.py` est réservé au
banc. Recompiler ensuite avec `D2_PROVISIONING_ENABLED=0`, sans effacer la NVS.
La période normale D2T2 est de 1800 s ; le provisionnement n'envoie pas
immédiatement un vecteur simulé. L'essai du firmware principal reste nécessaire.

`GSM_PWRKEY_PIN=-1` signifie carte à démarrage automatique ou module déjà allumé.
Si un transistor PWRKEY est validé et le module est éteint au départ : attente
alimentation 1 s, assertion 1,2 s, relâchement, attente 5 s, puis commandes AT.
Le niveau actif désigne le contrôle ESP32 du transistor. Ne pas connecter
directement un GPIO à PWRKEY sans vérifier le schéma. La séquence ne coupe pas
l'alimentation sur un échec réseau et ne repulse pas PWRKEY pendant les retries.
Si le modem reste alimenté lors d'un reset ESP32, utiliser le mode déjà allumé
ou ajouter une gestion STATUS adaptée au matériel avant d'activer une impulsion.

Le moteur vérifie AT/CPIN/CREG, sélectionne le mode texte, l'alphabet IRA sur UART
et DCS=0 pour GSM-7. Il attend `>` avant le SMS et Ctrl-Z, puis `+CMGS` et `OK`.
Commandes et attente réseau progressent sans boucle bloquante.

**Carte identifiée sur les photos fournies le 7 octobre 2026 :** SIM800C V1.34,
dos marqué « GSM-GPRS-BT SHIELD », « Designed By UNV », « 2017-02-10 » et
« DC:5V-9V IN ». Ce marquage indique que l'entrée d'alimentation de cette carte
accepte le 5 V. Il ne confirme pas le fonctionnement réel ni la capacité de
l'alimentation, qui reste à dimensionner. Le module nu fonctionne à 3,4–4,4 V
et présente des pointes de 2 A sur VBAT ; ces valeurs concernent le module,
pas directement le courant à l'entrée 5 V de la carte.
Le courant nominal du convertisseur seul ne garantit pas les performances pendant
les pointes ; valider câbles, découplage et chute de tension pendant émission.
La carte porte également les marquages « TTL UART » et « VMCU ». Le schéma exact
de cette révision n'a pas été confirmé : vérifier le rôle de VMCU, les niveaux
UART et le démarrage avant de fixer le câblage ESP32. Une entrée d'alimentation
compatible 5 V ne suffit pas à établir la compatibilité des signaux avec le 3,3 V.
Références : [SIMCom SIM800C](https://www.simcom.com/product/SIM800C.html),
[Hardware Design SIM800C §4.1–4.2](https://simcom.ee/documents/SIM800C/SIM800C_Hardware_Design_V1.05.pdf).

## Vérification et limites

Commandes reproductibles, sans flash ni SMS :

```text
python tests/sms/run_tests.py
python tests/dc_load/run_tests.py
python -m unittest discover -s "TEST UNITAIRE/SMS" -p test_provision_bench.py -v
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/sms/compile_profiles.ps1
python tests/sms/check_gateway_readonly.py --gateway-root C:\Users\Dell\Desktop\DJUA_SMS
```

Les tests PC comparent les 60 octets du packer, Base64URL, signed_part, HMAC et
SMS final aux vecteurs officiels. Ils vérifient aussi les erreurs officielles,
les plages, bits réservés, signes, vrais zéros, persistance, migration, retries,
pannes de stockage/UART, priorités et séquence PWRKEY. Les tests DC utilisent
les vrais registres du pilote avec un bus simulé, dans quatre configurations.
Le contrôle `check_gateway_readonly.py` désactive l'écriture des bytecodes Python
avant import et utilise uniquement parser/HMAC/normalisation, sans service,
SQLite, MQTT ou série. Sept fixtures C++ D2T2/D2E ont été acceptées par le code
réel de la passerelle avec les JSON attendus. Voir [résultats](VALIDATION_D2T2.md).

L'exemple suivant est le **vecteur public reproduit par le firmware sur PC**, pas
un vrai SMS envoyé par le boîtier :

```text
D2T2,DJUA-KIN-000001,AAAwOWq5NngHW80VA4R-WaCBdhPGDgAH0AB8RxgBM4AOAAARgj9B9AmNBZ4ABfgjA-AyAAGLAAHuAH_A,rHcRllcP4gs
```

Il transporte notamment 12,35 V / 3,2 A / 39,5 W / 19,76 Wh DC, longueur 113.
La clé publique des vecteurs reste uniquement une fixture de test, jamais une
clé provisionnée pour production.

Le banc ESP32 existant est adapté à D2T2 : autotest exact avec mbedTLS, SEND avec
mesures absentes, SEND VECTOR avec mesures **explicitement synthétiques**, RETRY
immutable, NVS conservée. Voir [guide de banc](../TEST%20UNITAIRE/SMS/README.md).

**Essai manuel communiqué ensuite par l'utilisateur :** import de clé confirmé
sur COM20, `SEND VECTOR` de séquence 5 accepté par le SIM800C (`+CMGS: 12`,
`queue_commit=OK`), puis reçu dans l'API le 7 octobre 2026 à 16:12:25 UTC avec
`protocol=D2T2`, `auth_status=VERIFIED` et les valeurs DC attendues.
La chaîne du banc est validée pour ce vecteur simulé. Après passage à GPIO32/13,
l'utilisateur a aussi indiqué que le nouvel essai était réussi, sans transmettre
le nouveau journal ou JSON. Le firmware principal et le troisième INA219/shunt
restent à vérifier sur le matériel. L'agent n'a pas lui-même téléversé ou envoyé ce SMS.
Voir [preuves et limites](VALIDATION_D2T2.md).
