# DJUA IoT — integration SMS D2T2 / D2E

Mise à jour du 7 octobre 2026 : les nouveaux messages périodiques sont **D2T2**,
avec charges DC. Le [guide D2T2](SMS_D2T2.md) précise le contrat actuel,
le shunt externe, le SIM800C et la migration. Les informations D2T ci-dessous
décrivent le format historique et les mécanismes de persistance conservés.

Reference receiver : `toussaintmadimba-cmyk/DJUA_SMS`, commit
`aac1101176e241a9e6ad2b1f5ec404adb3069a28`.

- [Specification figee](https://github.com/toussaintmadimba-cmyk/DJUA_SMS/blob/aac1101176e241a9e6ad2b1f5ec404adb3069a28/docs/sms_protocol_v2.md)
- [Vecteurs officiels](https://github.com/toussaintmadimba-cmyk/DJUA_SMS/blob/aac1101176e241a9e6ad2b1f5ec404adb3069a28/tests/vectors/d2_conformance.json)
- Copie de test locale : `tests/sms/d2_conformance.json` (cle PUBLIQUE de test).

## Profils exclusifs

`src/config.h` expose `DJUA_COMM_PROFILE` :

| Valeur | Profil | Transport du boitier |
| --- | --- | --- |
| 1 (defaut) | `DJUA_PROFILE_SMS_PRODUCTION` | SMS uniquement, aucun Wi-Fi/MQTT/HTTP |
| 2 | `DJUA_PROFILE_MQTT_TEST` | Wi-Fi puis MQTT, aucun SMS/POST HTTP |
| 3 | `DJUA_PROFILE_HTTP_TEST` | Wi-Fi puis POST HTTP, aucun SMS/MQTT |

Selection dans `config.h`, ou avec
`--build-property "compiler.cpp.extra_flags=-DDJUA_COMM_PROFILE=2"` / `=3`.
Une periode de 30 minutes represente 48 SMS periodiques par jour en fonctionnement
normal, auxquels s'ajoutent urgences et retries ; verifier le forfait operateur.

L'acquisition reste a 10 s. Les profils de test conservent le JSON historique
et les energies rectangulaires historiques : ils ne constituent pas un test D2
bout en bout. En HTTP_TEST, la geofence et les alertes restent locales ; aucun
endpoint HTTP d'evenement n'est invente. En MQTT_TEST, les snapshots et evenements
geofence existants restent disponibles. Aucun basculement automatique entre canaux.

## Flux de production

```text
capteurs toutes les 10 s -> fenetre energetique -> D2T toutes les 1800 s
GPS/geofence toutes les 5 s -> transition confirmee -> D2E GX/GE
                          -> compteur commun reserve en NVS
                          -> construction + HMAC + SMS immutable en NVS
                          -> priorite urgences -> UART1/AT -> operateur SMS
                          -> SIM868 DJUA_SMS -> SQLite/outbox -> MQTT -> backend
```

La machine geofence historique reste inchangee : son etat confirme est en RAM.
Au demarrage hors zone, elle etablit OUTSIDE sans produire un faux EXIT initial.
Seuls les SMS deja enregistres dans la file survivent au reboot ; une transition
interrompue avant l'enregistrement durable n'est pas garantie conservee.

Le firmware s'arrete a l'acceptation `+CMGS` suivie de `OK`. Ce n'est PAS un accuse
de reception applicatif. D2 ne definit ni SMS ACK du recepteur, ni preuve que le
backend a traite le message. Un timeout de soumission est incertain : retry des
memes octets, deduplication par `message_id` au recepteur. Apres acceptation modem,
la suppression durable du message de la file arrete les retries. Une panne entre
acceptation et suppression peut produire un doublon, pas une nouvelle identite.
Une publication perdue par l'operateur apres acceptation ne peut pas etre detectee
avec ce contrat. Aucune garantie de livraison bout en bout n'est revendiquee.

## Mesures et energie

En D2, une mesure absente est `-`, un vrai zero est `0`. Les flags doivent
correspondre exactement aux champs presents. Ni saturation, ni concatene,
ni troncature : un SMS trop grand est refuse (160 septets maximum).

Les groupes batterie/solaire/AC sont horodates a la fin de chaque lecture et doivent
avoir au plus 20 s lors de la construction. Le GPS doit etre valide et avoir au plus
5 s. La RTC locale GMT+1 est convertie en secondes Unix UTC ; `UP` reste `millis()`.
La production n'initialise pas une RTC invalide avec l'heure de compilation.

Les trapezes d'energie solaire et AC utilisent l'instant logique de fin de
l'acquisition electrique groupee (la fenetre AC existante dure environ 1 s).
Les acquisitions restent donc des estimations de puissance, pas un compteur
d'energie certifie. Une acquisition invalide ou un ecart >20 s rend l'energie
incomplete jusqu'a la fin de la fenetre. Aucune extrapolation. La premiere
acquisition a/apres l'echeance termine A et commence B sans double comptage.
Une frontiere invalide rend A et B incompletes. Le prochain intervalle progresse
meme pendant un retry ou un echec de mise en file. `INT` est la duree reelle arrondie.
Si cette duree depasse 86400 s, le codec refuse le message ; il ne reduit pas `INT`.

Signes a verifier physiquement : batterie positive = charge, solaire positif =
production, AC actif positif = consommation. `BATTERY_CURRENT_POLARITY`,
`SOLAR_CURRENT_POLARITY` et la calibration/polarite AC permettent l'orientation.
Les plages D2 ne relevent JAMAIS les limites electriques des capteurs installes.

## Identite, compteur et file persistante

Namespace Preferences/NVS : `djua_d2` ; enregistrements versionnes `identity`,
`sequence`, `queue`, avec checksum de corruption en plus de la NVS.
La calibration AC existante n'est ni effacee ni reutilisee pour le compteur.

Un appareil vierge est `D2_UNPROVISIONED`, sans creation/envoi D2. Le
provisionnement explicite donne identite technique, cle 32 octets, numero E.164 du
recepteur, reference d'installation et compteur initial. `DEVICE_ID` reste
l'identifiant des anciens profils de test ; le DEV des SMS vient de la NVS.
Le recepteur/backend doivent enregistrer cette identite technique et son association
au numero de la SIM EMETTRICE. Le numero du RECEPTEUR en NVS sert a la destination.

Les sequences D2T/D2E partagent une reservation par blocs de 256, persistee AVANT
utilisation. Un reboot abandonne les numeros reserves restants ; les trous sont
autorises. Aucun wrap uint32. Compteur manquant/corrompu : `D2_COUNTER_FAULT`.
Identite/file corrompue ou ecriture echouee : `D2_STORAGE_FAULT`. Derniere sequence
consommee : `D2_SEQUENCE_EXHAUSTED` ; les SMS deja valides en file restent envoyables.
Les ecritures incertaines bloquent le service jusqu'a diagnostic/redemarrage.

La file conserve 16 SMS au maximum, dont au plus 4 periodiques : 12 places restent
reservees aux urgences. Pas de remplacement/ecrasement silencieux. File pleine :
message refuse avec `MESSAGE NON CREE`, alerte locale et compteur d'echecs ;
un evenement REFUSE n'est pas durablement cree. Cette file ne garantit donc pas
la conservation de toutes les transitions pendant une panne illimitee.
Les urgences eligibles passent avant les periodiques, sans interrompre un CMGS
deja engage. Backoff : 30 s, 60 s, 120 s... plafonne a 15 min ; au reboot les messages
persistes sont immediatement eligibles. Les octets et la sequence ne changent jamais.

Il n'existe pas d'effacement/reset automatique du compteur. Une ancienne sauvegarde
NVS ne doit pas etre restauree : la NVS seule ne peut pas prouver l'absence de rollback.
Apres perte irreparable ou remplacement ESP32, nouvelle identite technique obligatoire.
Une NVS totalement effacee est indiscernable d'un appareil neuf : elle redevient
UNPROVISIONED, jamais automatiquement READY/sequence 1.

## Provisionnement local supervise

Desactive par defaut : `D2_PROVISIONING_ENABLED=0`. Pour l'installation seulement,
compiler avec `=1`, utiliser la console USB (115200 bauds, fin de ligne LF) :

```text
D2 STATUS
D2 PROVISION <TECHNICAL_ID> <SECRET_64_HEX> <RECEIVER_E164> <INSTALLATION_REFERENCE> <NEXT_SEQUENCE_DECIMAL>
```

Les parametres doivent etre separes par des espaces ; reference <=64 caracteres,
sans espace ; sequence 1..4294967295. NVS deja renseignee : refus, aucune mise a zero.
Generer 32 octets cryptographiquement aleatoires par appareil hors du depot, transmettre
la meme cle a DJUA_SMS de maniere sure et configurer son sender binding. Le programme
n'affiche pas la cle et refuse la cle publique officielle de test. Ne pas enregistrer
la commande dans un terminal partage, un fichier versionne ou une capture d'ecran.
Recompiler ensuite avec `D2_PROVISIONING_ENABLED=0`, sans effacer la flash/NVS.
La console AC est suspendue pendant ce mode pour eviter deux consommateurs USB.

Attention : les secrets NVS ne sont pas chiffres automatiquement. HMAC authentifie
le contenu mais ne chiffre ni le SMS ni les positions. La securite physique, le
chiffrement flash/NVS et la protection d'une console USB ne sont pas livres ici.

## Materiel et caractere underscore

Recepteur connu : SIM868. Emetteur : SIM800C, carte marquee DC:5V-9V IN.
Apres validation du banc par l'utilisateur, le firmware principal utilise
`GSM_HARDWARE_CONFIGURED=1`, RX32/TX13 et 9600 bauds, sans GPIO PWRKEY. Aucun APN/GPRS.
Le provisionnement NVS du firmware principal et son essai reel restent necessaires.
UART1 est reserve au GSM ;
UART2 appartient au GPS. Les conflits avec les autres capteurs/LED/buzzer, les broches
flash/strapping et les sorties sur GPIO d'entree seulement sont refuses a la compilation.
Le modem doit deja etre allume ; aucun timing PWRKEY n'est suppose.

Le pilote configure `CMGF=1`, `CSCS="IRA"` (ASCII sur UART), `CSMP=17,167,0,0`
(DCS=0 : GSM-7 sur SMS). Le code ASCII de `_` est 0x5F, son code GSM-7 est 0x11 :
il faut une conversion explicite pour que le HMAC arrive intact.
[Manuel AT SIMCom, sections CSCS/CMGS](https://files.waveshare.com/upload/2/26/SIM800_Series_AT_Command_Manual_V1.11.pdf).
Le recepteur au SHA de reference configure CMGF mais ne force pas CSCS : verifier
son charset effectif (lecture `AT+CSCS?`) et l'exactitude de `_` avec un vrai SMS.
Un alignement explicite du charset du recepteur doit etre gere dans DJUA_SMS,
pas par une modification de la grammaire ni une alteration du contenu signe.

## Verification reproductible

```powershell
python tests/sms/run_tests.py
powershell -NoProfile -File tests/sms/compile_profiles.ps1
```

Le premier lanceur compile le vrai noyau C++ avec MSVC/BCrypt dans un dossier
temporaire : 7 encodages exacts, 14 validations wire, longueurs et scenarios
energie/compteur/file/pannes modem/priorite/reboot. Le sender binding est uniquement
cote recepteur : son vecteur a un SMS/HMAC valide cote firmware. Le deuxieme compile
trois profils plus la branche UART/provisionnement dans des copies temporaires,
sans modifier config.h du depot et sans flash. Ses GPIO ne sont que des fixtures
de compilation, PAS un cablage recommande.

Restent obligatoires avant validation terrain : vrais SMS D2T/D2E avec SIM868,
underscores/signatures, pertes d'alimentation reelles NVS, polarites/calibrations,
reception durable, et validation/idempotence du backend reel.

## Bilan logiciel du 5 octobre 2026

- Tests D2 : 4 tests Python, 7 encodages officiels exacts, 14 validations wire
  (sender binding hors role emetteur), 6 groupes de scenarios C++ : PASS.
- Indicateurs : 7 variantes (dont les profils HTTP/MQTT) : PASS.
- Solaire : 4 variantes : PASS. API existante : 4 tests : PASS.
- Compilation ESP32 : SMS_PRODUCTION, MQTT_TEST, HTTP_TEST et branche
  UART/provisionnement activee uniquement dans une copie de compilation : PASS.
- Aucun flash, aucun commit/push, aucune ecriture dans DJUA_SMS.

Des avertissements preexistants RTC/ArduinoJson/TinyGPSPlus/INA219 restent visibles.
Les tests simules ne prouvent ni l'atomicite d'une coupure flash reelle, ni la
livraison operateur, ni le traitement metier du backend reel.
