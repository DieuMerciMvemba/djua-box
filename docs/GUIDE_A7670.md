# Guide complet de mise en service A7670 et SMS

Ce guide rassemble le choix du modem et de la SIM, les numéros à préparer,
le câblage, la vérification AT, le provisionnement local de la box et l'essai
SMS. Il décrit le firmware de ce dépôt ; la carte A7670 exacte et l'opérateur
restent à valider physiquement avant mise en service.

Pour les documents constructeur, consulter la [fiche produit officielle
A7670X](https://cn.simcom.com/product/A7670X.html) pour les variantes et bandes,
puis la [page des téléchargements SIMCom](https://en.simcom.com/product/A7670X.html)
pour la fiche technique correspondant à la variante, le *Hardware Design*
A7670 et le manuel AT A76XX. SIMCom distingue notamment A7670C, A7670E et
A7670SA : leurs bandes radio ne sont pas identiques. La fiche indique
3,4–4,2 V pour le module nu ; cela ne définit pas l'entrée d'alimentation d'une
carte porteuse.

## 1. Comprendre les numéros

Il y a au moins trois identifiants téléphoniques différents. Les confondre
est une cause fréquente de SMS qui partent vers le mauvais appareil.

| Élément | Rôle dans ce projet | Où le relever |
| --- | --- | --- |
| Numéro de la SIM de la box (`MSISDN`) | Identifie l'expéditeur des SMS de cette box ; le récepteur doit l'associer au bon appareil | Espace opérateur / inventaire SIM. Ne pas supposer que le modem sait le lire |
| Numéro de destination (`receiver`) | Numéro auquel **la box envoie** les SMS ; stocké dans la NVS de l'ESP32 | Numéro de la SIM du modem de réception / passerelle SMS |
| Numéro de la SIM de réception | Reçoit les messages envoyés par les box ; cette passerelle doit ensuite reconnaître l'expéditeur | Configuration opérateur et configuration DJUA_SMS |

La SIM installée dans l'A7670 est donc l'expéditeur. Le champ `receiver` de
`D2 PROVISION` est le numéro de la passerelle qui reçoit. La passerelle doit
connaître le numéro expéditeur et l'associer à l'identité technique et à la
clé HMAC correspondantes. Le numéro expéditeur ne remplace pas l'identifiant
technique `DEVICE_ID`.

Utiliser le format international E.164 pour `receiver` : `+`, indicatif pays,
puis le numéro national normalisé, sans espaces, tirets ni parenthèses. Le
firmware accepte `+` suivi de 1 à 15 chiffres, dont le premier n'est pas zéro.
Cette validation vérifie la forme, pas l'attribution réelle du numéro par
l'opérateur.

## 2. Approvisionner la SIM et choisir la variante

Avant de monter la SIM dans la box, obtenir de l'opérateur ou du fournisseur
IoT les informations suivantes :

- SIM activée, non suspendue, non bloquée et compatible avec la taille du
  support de la carte porteuse ;
- service SMS **sortant (MO)** autorisé sur cet abonnement, crédit ou forfait
  suffisant, et SMS internationaux autorisés si la passerelle est à l'étranger ;
- couverture et itinérance permises à l'emplacement prévu ;
- numéro attribué à la SIM expéditrice, ICCID, opérateur, forfait et contact
  de remplacement consignés dans un inventaire protégé ;
- code PIN de SIM connu par l'administrateur. Pour ce firmware, le plus simple
  est une SIM configurée sans demande de PIN avant insertion : le programme
  sait détecter une SIM non prête, mais ne saisit ni PIN ni PUK.

Ne jamais tenter des PIN/PUK au hasard. Une SIM verrouillée peut finir bloquée
et exiger le déblocage de l'opérateur.

Lire la référence exacte imprimée sur le module ou la carte : A7670C, A7670E,
A7670SA, révision éventuelle et modèle de la carte porteuse. Comparer les
bandes radio de cette variante avec celles utilisées par l'opérateur **au site
d'installation**. Le nom « A7670 » seul ne suffit pas à prouver la couverture.
Vérifier aussi que l'opérateur autorise SMS MO avec cette combinaison de module,
SIM et réseau LTE. Le support SMS et l'inscription LTE varient selon le réseau
et l'offre ; un APN n'est pas nécessaire au parcours SMS seul de ce firmware.

Le centre SMS (SMSC) est généralement fourni/configuré par l'opérateur ou la
SIM. Ne pas inventer de numéro SMSC et ne pas le modifier à l'aveugle. Ce
firmware lit `AT+CSCA?`, puis refuse l'envoi si aucun centre SMS valide n'est
présent.

## 3. Câblage et configuration actuelle du projet

Le projet cible un ESP32 et configure le modem sur UART1 :

| Signal | ESP32 | Carte A7670 |
| --- | --- | --- |
| RX ESP32 | GPIO32 (`GSM_RX_PIN`) | TXD modem |
| TX ESP32 | GPIO13 (`GSM_TX_PIN`) | RXD modem |
| Référence électrique | GND | GND |
| PWRKEY | Désactivé par défaut (`-1`) | À câbler seulement si le schéma de la carte le permet |

RX et TX se croisent. Le débit configuré est 115200 bauds, 8N1. Le GPS conserve
UART2 GPIO16/17. GPIO14 est déjà affecté au TX logiciel du PZEM : ne pas
l'utiliser pour PWRKEY.

**Avant de brancher l'ESP32**, vérifier dans le schéma de la carte porteuse :

1. la tension et le courant requis à son entrée d'alimentation, les pointes de
   courant radio et le câblage de l'antenne LTE ;
2. les niveaux logiques et la direction des broches UART ;
3. le support SIM, son orientation et l'antenne principale ;
4. le circuit de commande PWRKEY, s'il existe.

Ne pas appliquer directement 5 V au module nu : ses limites d'alimentation
diffèrent de celles d'une carte porteuse pouvant intégrer un régulateur. Ne
pas raccorder PWRKEY directement à un GPIO sans le schéma de la carte ; le
firmware prévoit un pilotage par étage de commande validé.

Dans `src/config.h`, `GSM_HARDWARE_CONFIGURED` vaut actuellement `0` par
défaut : l'UART modem reste inactif. Ne le passer à `1` qu'après validation
électrique et UART. `GSM_PWRKEY_PIN=-1` signifie que le modem est alimenté
séparément ou démarre automatiquement ; le programme attend alors le délai de
démarrage avant de lancer les commandes AT.

## 4. Vérifier le modem et le réseau avec le banc AT

Le sketch [banc AT A7670](../TEST%20UNITAIRE/A7670/a7670_at_test/a7670_at_test.ino)
sert de passerelle série manuelle. Il n'envoie aucun SMS automatiquement et
n'efface aucune NVS. Suivre [l'essai firmware GSM](TEST_FIRMWARE_GSM.md) pour
son téléversement. Le port USB de l'ESP32 et l'UART du modem sont deux ports
différents.

Ouvrir le moniteur USB à 115200 bauds et envoyer les commandes avec fin de
ligne CR. Commencer par ces lectures sans effet de bord :

| Commande | Résultat à vérifier |
| --- | --- |
| `AT` | Réponse `OK` : UART, débit et masse cohérents |
| `ATI` | Identification modem/firmware cohérente avec la variante attendue |
| `AT+CPIN?` | `+CPIN: READY` ; sinon traiter la SIM avec l'opérateur, pas avec des essais au hasard |
| `AT+CEREG?` | Inscription LTE ; `stat=1` (réseau d'origine) ou `stat=5` (itinérance) est généralement attendu |
| `AT+CREG?` | État de l'enregistrement circuit ; le firmware combine ce résultat avec `CEREG` |
| `AT+CSMS?` | Le support SMS sortant doit être annoncé ; le firmware exige `MO=1` dans la réponse |
| `AT+CSCA?` | Centre SMS non vide et valide selon l'opérateur |
| `AT+CSQ` | Indication de signal utile au diagnostic, pas preuve d'un SMS fonctionnel |

`CEREG`/`CREG` sans inscription ne signifie pas toujours une panne matérielle :
vérifier couverture, bandes, antenne, itinérance et restriction d'abonnement.
La syntaxe et les valeurs détaillées sont à confirmer dans le manuel AT de la
révision du modem.

Pour prouver le trajet réel, un essai d'envoi est nécessaire et peut être
facturé. Le faire d'abord vers un téléphone ou une passerelle contrôlée, avec
l'autorisation de l'opérateur. Le banc AT est manuel : après `AT+CMGS="+..."`,
attendre `>`, saisir un message court, puis envoyer le caractère Ctrl+Z pour
soumettre. Le numéro d'exemple doit être remplacé par un destinataire de test
que l'équipe contrôle. Ne pas publier les réponses AT si elles contiennent des
identifiants de SIM ou des numéros privés.

## 5. Préparer le récepteur et attribuer les identités

Avant de provisionner une box, créer son entrée côté passerelle/récepteur :

1. réserver un `DEVICE_ID` unique, en majuscules, chiffres et tirets, de 1 à
   32 caractères ;
2. générer une clé HMAC aléatoire de 32 octets (64 caractères hexadécimaux) ;
3. associer cette identité à la clé dans le récepteur et autoriser le numéro
   expéditeur de la SIM installée dans cette box ;
4. définir la politique de séquence attendue côté récepteur et choisir la
   première séquence à utiliser par l'ESP32 ;
5. relever le numéro de la passerelle réceptrice au format E.164 ; c'est cette
   valeur qui sera copiée dans `receiver` sur la box.

Chaque box doit posséder sa propre identité et sa propre clé. Le compteur doit
commencer à 1 ou plus, ne jamais être remis à zéro après usage et être cohérent
avec le récepteur. La NVS réserve les séquences par blocs ; des trous après un
redémarrage sont normaux.

## 6. Provisionner la box (ESP32)

Le provisionnement de production écrit identité, clé, numéro de réception,
référence d'installation et compteur initial dans la NVS ESP32 (`djua_d2`).
Il est distinct de l'approvisionnement opérateur de la SIM.

1. Confirmer que le récepteur possède déjà l'identité et la clé qui seront
   chargées dans la box.
2. Sauvegarder la configuration de build. Activer temporairement
   `D2_PROVISIONING_ENABLED=1` dans `src/config.h`, compiler et téléverser le
   firmware de provisionnement. Garder le modem déconnecté ou désactivé pendant
   cette étape si aucun envoi radio n'est souhaité. La télémétrie peut tout de
   même être mise en file après le provisionnement ; le modem ne doit être
   activé que lorsque ces messages sont destinés à partir. La file n'est pas
   effacée automatiquement.
3. Ouvrir le moniteur USB ESP32 à 115200 bauds. Envoyer `D2 STATUS`. Un appareil
   neuf doit répondre avec l'état `D2_UNPROVISIONED`.
4. Envoyer une seule fois la commande suivante, avec les vraies valeurs
   fournies au récepteur :

   ```text
   D2 PROVISION <DEVICE_ID> <CLE_HMAC_64_HEX> <NUMERO_PASSERELLE_E164> <REFERENCE_SANS_ESPACES> <SEQUENCE_INITIALE>
   ```

   La référence doit être non vide et faire au plus 64 caractères. Le parseur
   série sépare sur les espaces ; elle ne peut donc pas contenir d'espaces.
   Le compteur est décimal, entre 1 et 4294967295.
5. Attendre `Provisionnement durable OK`, puis `D2 STATUS` et vérifier
   `D2_READY`. Ne pas copier la clé dans un terminal avec enregistrement,
   journal de build, capture d'écran ou ticket. L'USB n'est pas un canal
   chiffré ; la personne qui contrôle le câble peut observer la commande.
6. Remettre `D2_PROVISIONING_ENABLED=0`, recompiler et téléverser le firmware
   normal. Cette option est déjà désactivée par défaut. Une mise à jour normale
   du firmware conserve la NVS ; **ne pas effacer les préférences/flash**.
7. Ranger la clé dans le gestionnaire de secrets du récepteur. Une clé perdue
   ne se récupère pas en lisant la NVS. Le journal refuse le reprovisionnement
   d'une NVS déjà initialisée ; ne pas contourner ce refus par un effacement.

Le provisionnement est sensible : la clé est transmise en clair sur l'USB et
gardée dans la NVS locale, qui n'est pas automatiquement chiffrée. La console
ne fournit pas une authentification d'opérateur. Faire l'opération en atelier
contrôlé, désactiver l'enregistrement du terminal et retirer la clé de toute
sortie de diagnostic.

## 7. Vérifier un SMS de bout en bout

Activer `GSM_HARDWARE_CONFIGURED=1` uniquement une fois la carte validée et
installer le firmware normal avec `D2_PROVISIONING_ENABLED=0`. Confirmer que
la production utilise le profil SMS (`DJUA_COMM_PROFILE` vaut
`DJUA_PROFILE_SMS_PRODUCTION`). Vérifier ensuite les étapes dans cet ordre :

1. le journal affiche `D2_READY` ;
2. le modem répond aux commandes d'initialisation, la SIM est prête, le réseau
   est enregistré, le SMS MO est disponible et le SMSC est valide ;
3. le firmware place le SMS en file NVS ;
4. à l'envoi, le modem affiche le prompt `>`, puis `+CMGS` et `OK` ;
5. le téléphone ou la passerelle de réception reçoit le SMS ;
6. la passerelle vérifie l'expéditeur, l'identité, la séquence et le HMAC, puis
   transmet les données au backend selon le contrat configuré.

La cadence actuelle `D2_PERIOD_SECONDS` vaut 30 secondes dans `config.h`.
Après provisionnement, le firmware peut créer des SMS périodiques sans action
manuelle. Prévoir un numéro contrôlé et surveiller la file et la consommation
de SMS lors du premier démarrage. Si `D3_RECEIVER_ENABLED` est activé, valider
le contrat [D3](SMS_D3.md) et les deux SMS d'une paire avec la passerelle.

`+CMGS` puis `OK` confirme l'acceptation par le modem/opérateur, pas la
réception finale ni le traitement du backend. L'interface conserve les SMS
non confirmés et les réessaie ; le même texte signé peut donc arriver plusieurs
fois. La passerelle et le backend doivent gérer les doublons conformément au
[contrat de données](CONTRAT_BACKEND_D2.md).

## 8. Dépannage rapide

| Symptôme | Contrôle suivant |
| --- | --- |
| Aucune réponse à `AT` | Alimentation, GND commun, TX/RX croisés, niveaux logiques, bon UART, bon baud et modem réellement démarré |
| `SIM ABSENTE/VERROUILLEE` | Orientation/contacts SIM, statut opérateur, PIN SIM ; ne pas tenter de PUK au hasard |
| `RESEAU NON ENREGISTRE` | Variante et bandes, antenne, couverture, roaming, statut de ligne et restriction LTE/SMS de la SIM |
| `SERVICE SMS MO NON DISPONIBLE` | Option SMS sortant du forfait et support opérateur ; vérifier `AT+CSMS?` |
| `CENTRE SMS NON CONFIGURE` | Demander le SMSC correct à l'opérateur ; ne pas saisir une valeur trouvée au hasard |
| `+CMS ERROR` / `+CME ERROR` | Noter le code sans publier de données SIM, puis consulter le manuel AT et l'opérateur |
| `+CMGS`/`OK`, rien au destinataire | Confirmer le numéro destinataire, le SMSC, le filtrage passerelle, les frais et la route opérateur |
| `D2_UNPROVISIONED` | L'identité n'a pas été écrite : vérifier compilation avec le provisionnement temporaire et commande complète |
| `D2_STORAGE_FAULT` ou `D2_COUNTER_FAULT` | Arrêter les essais ; préserver la NVS et suivre une procédure de récupération dédiée, sans effacement improvisé |
| SMS reçu mais HMAC rejeté | Vérifier identité, clé côté passerelle, numéro expéditeur autorisé et compteur ; ne pas régénérer une clé isolément |

## 9. Secrets et changements de SIM

- Ne jamais committer une clé HMAC, PIN/PUK, ICCID ou une configuration privée.
- Un HMAC authentifie les données mais ne les chiffre pas. Le SMS peut révéler
  son contenu à l'opérateur et au récepteur.
- Changer la SIM expéditrice peut changer le numéro d'origine : mettre à jour
  l'autorisation expéditeur côté passerelle avant le déploiement. Le numéro
  `receiver` dans la NVS ne change que si la passerelle destinataire change.
- Un remplacement de modem ou une migration NVS peut affecter compteur et file.
  Ne jamais cloner une NVS de production vers plusieurs box : cela dupliquerait
  identité, clé et séquences.
- Toute modification de numéro, SIM, opérateur, variante radio ou firmware doit
  être suivie d'un SMS réel contrôlé et d'une vérification au récepteur.

## 10. Fichiers du projet utiles

- [`src/config.h`](../src/config.h) — profil de transport, activation matérielle,
  UART, temporisations et option de provisionnement temporaire.
- [`src/pins.h`](../src/pins.h) — brochage ESP32 du modem, GPS et PZEM.
- [`src/communication/gsm.cpp`](../src/communication/gsm.cpp) — séquence AT,
  contrôle réseau/SIM/SMSC et protocole `CMGS`.
- [`src/communication/sms.cpp`](../src/communication/sms.cpp) — console USB,
  écriture NVS et état du journal.
- [`docs/SMS_D2.md`](SMS_D2.md) et
  [`docs/TEST_FIRMWARE_GSM.md`](TEST_FIRMWARE_GSM.md) — journal, essais opérateur
  et critères de validation existants.

