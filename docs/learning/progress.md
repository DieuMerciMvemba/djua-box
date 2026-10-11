# Progression

## Point de départ

Cette première passe reconstruit la compréhension du dépôt à partir du code
actuellement présent. Aucun concept n'est considéré comme maîtrisé :
le projet a été largement produit avec l'aide de l'IA et la compréhension n'a
pas encore été évaluée.

Le système observé est une chaîne IoT composée de deux éléments dans ce dépôt :

- un firmware Arduino pour ESP32 qui mesure l'énergie, la position et l'heure,
  décide localement de l'état geofence et publie les résultats ;
- une API Python/FastAPI qui reçoit les messages MQTT et les expose en HTTP et
  WebSocket.

Le dashboard, le backend qui reçoit le POST HTTP direct et une éventuelle base
de données ne sont pas présents dans ce dépôt.

## Concepts compris

Aucun concept n'est encore validé comme compris. Cette section sera alimentée
après des questions, des explications reformulées par l'utilisateur ou des
décisions techniques prises consciemment.

## Indispensables maintenant

Statut initial : **à vérifier** ou **à apprendre**.

| Concept | Statut | Pourquoi maintenant |
| --- | --- | --- |
| Architecture firmware par modules | À vérifier | Savoir relier le point d'entrée, les capteurs, la télémétrie et les transports. |
| Cycle `setup()` / `loop()` | À apprendre | Comprendre quand les composants sont initialisés et à quelle cadence ils travaillent. |
| Structures de données et contrat JSON | À apprendre | Suivre une mesure depuis une structure C++ jusqu'au message reçu par l'API. |
| MQTT : broker, topic, retained, QoS | À apprendre | Relie l'ESP32 à l'API en profil de test ; relie DJUA_SMS au backend en production SMS. |
| Validité d'une mesure | À apprendre | Distinguer une vraie valeur zéro d'une mesure absente ou invalide. |
| Horodatage RTC et temps depuis le démarrage | À vérifier | Le RTC fournit la date transmise ; `millis()` sert uniquement aux durées internes. |

## Importants prochainement

| Concept | Statut | Pourquoi prochainement |
| --- | --- | --- |
| Machine d'état, hystérésis et confirmation | À apprendre | La geofence et plusieurs indicateurs dépendent d'un état conservé entre les boucles. |
| Code bloquant et code non bloquant | À apprendre | Les mesures AC et les appels réseau peuvent retarder la boucle principale. |
| Calibration, filtrage et NVS | À apprendre | La qualité des mesures dépend du matériel et de coefficients persistants. |
| API HTTP et WebSocket | À vérifier | L'API transforme les messages MQTT en interfaces consommables par un futur client. |
| Stockage volatil et persistant | À apprendre | L'API perd son historique au redémarrage, contrairement aux calibrations AC en NVS. |
| États, alertes et priorités | À apprendre | `device_status` transforme plusieurs pannes en un état LED/buzzer unique. |

## Avancés

| Concept | Statut | Pourquoi il peut attendre |
| --- | --- | --- |
| Échantillonnage AC, RMS, puissance active et facteur de puissance | À apprendre plus tard | Important pour la précision électrique, mais pas nécessaire pour comprendre d'abord l'architecture. |
| Concurrence entre thread MQTT et serveur FastAPI | À apprendre plus tard | Le verrou protège l'état partagé, mais ce détail vient après le flux général. |
| Fiabilité de livraison et tolérance aux coupures | À apprendre plus tard | Nécessaire avant production : files persistantes, accusés, reprise et idempotence. |
| Sécurité réseau IoT | À apprendre plus tard | TLS, gestion des secrets et authentification devront être conçus avant production. |
| Contraintes mémoire et temps réel de l'ESP32 | À apprendre plus tard | À approfondir lors de l'optimisation ou du diagnostic de pertes de données. |

## Non prioritaires

Ces sujets ne sont pas nécessaires pour comprendre l'état actuel, car ils sont
absents ou seulement esquissés :

- validation matérielle du modem émetteur : modèle et câblage encore inconnus ; le logiciel D2 existe désormais (voir ci-dessous) ;
- frontend/dashboard : aucun code frontend dans le dépôt ;
- base de données et migrations : aucune base n'est utilisée par l'API actuelle ;
- architecture de déploiement de production : aucun Dockerfile, pipeline CI ou
  manifeste d'infrastructure n'est présent ;
- microSD : seulement prévue dans un commentaire de brochage.

## À revoir

À remplir après le premier checkpoint. Points candidats déjà sensibles :

- **Séquence SMS** : numéro de chaque nouveau message du boîtier, commun à D2T2
  et D2E. Explication demandée dans le chat ; compréhension encore à vérifier.
  Le retry conserve ce numéro ; le passage D2T → D2T2 ne remet pas le compteur à zéro.

- différence entre le timestamp RTC, `millis()` et l'heure UTC de réception de l'API ;
- différence entre publication MQTT réussie et traitement métier confirmé ;
- différence entre valeur nulle, valeur zéro et valeur invalide ;
- différence entre mémoire vive, NVS, RTC et future base de données.

## Prochain checkpoint

Le premier checkpoint doit vérifier que l'utilisateur peut :

1. dessiner le trajet d'une mesure solaire jusqu'à un client HTTP ;
2. expliquer ce qui est perdu lors du redémarrage de l'ESP32 puis de l'API ;
3. identifier où changer une adresse I2C, un topic MQTT et une limite d'historique ;
4. expliquer pourquoi l'API locale ne reçoit pas le POST HTTP direct ;
5. distinguer mesure, décision locale, transport et présentation.

## Nouveaux concepts — intégration D2

Statut : rencontrés dans le code, **compréhension non évaluée**.

- **Contrat wire et HMAC** : D2 signe les octets exacts du SMS ; changer une
  virgule ou `_` pendant un retry invalide l'authentification. HMAC ne chiffre pas.
- **Persistance et idempotence** : réserver les séquences avant usage et garder
  le SMS en NVS évite les nouveaux identifiants après coupure. Le récepteur
  déduplique les retries ; `+CMGS` ne prouve pas la réception du backend.
- **Fenêtre énergétique et frontière** : les acquisitions toutes les 10 s et
  les SMS toutes les 30 min sont deux cadences différentes. La mesure frontière
  termine une période et commence la suivante sans doubler l'énergie.

À revoir lors du prochain checkpoint : pourquoi ne pas réinitialiser le compteur
à 1, pourquoi une énergie incomplète n'est pas zéro, et pourquoi HTTP/MQTT sont
des profils de test exclusifs plutôt qu'un fallback de production.

## Essais SMS materiels — 6 octobre 2026

Resultats rapportes par l'utilisateur dans le chat : SIM800C emetteur a 9600
bauds, SIM868 recepteur ; D2T recu dans l'API, doublon reconnu, signature HMAC
calculee sur PC acceptee, contenu altere refuse avec AUTH_INVALID, cle locale
persistante et demarrage automatique apres redemarrage Windows verifies.
FIELD_COUNT a ete diagnostique comme un champ '-' supplementaire (23 au lieu de 22).

Etape en preparation : `TEST UNITAIRE/SMS/d2_esp32_hmac_test` calcule la signature
sur ESP32 avec le vrai codec, provisionnement USB sans afficher la cle et NVS
de banc distincte. Compilation/tests PC ne remplacent pas le futur essai materiel.
Ne pas considerer l'integration aux capteurs ou la securite de production valides.

A revoir : difference entre une trame signee sur PC et une trame construite par
le firmware, entre cle provisoire de CMD et fichier persistant, et entre sequence
D2 et compteur de reception API. Comprehension non evaluee par un checkpoint.

## Nouveaux concepts — D2T2 et troisième INA219, 7 octobre 2026

Statut : utilisés dans l'intégration, **compréhension non évaluée**.

- **Packing binaire** : les mesures occupent 480 bits selon un ordre précis,
  puis Base64URL les représente en 80 caractères pour un SMS unique.
- **Shunt externe et calibration** : pour l'exemple 50 A / 75 mV, le pilote
  calcule la résistance de 1,5 mΩ puis la calibration des registres INA219.
  Ce calibre logiciel ne valide pas le montage ni le dimensionnement matériel.
- **Absence canonique** : un capteur DC non configuré transmet des emplacements
  binaires zéro avec flags éteints ; le backend reçoit null, pas une consommation nulle.

À revoir : pourquoi l'adresse I²C doit être différente sur chaque INA219,
pourquoi une énergie incomplète doit être absente, et pourquoi une alimentation
5 V de carte SIM800C doit être distinguée du VBAT du module nu.

## Essai GSM du banc puis préparation du firmware principal

L'utilisateur a fourni `KEY_OK`, l'acceptation SIM800C puis le JSON API
`D2T2`/`VERIFIED`/séquence 5, avec valeurs DC simulées. Après passage du banc
à RX32/TX13, il a annoncé un nouvel essai réussi. Ces broches sont reprises dans
le firmware principal ; le troisième INA219 physique est encore absent.

À comprendre : le provisionnement du banc (`djua_d2_bench`) et celui du firmware
principal (`djua_d2`) sont séparés. La validation du banc ne remplace pas l'essai
du firmware avec ses capteurs réels. Un groupe DC absent n'empêche pas l'envoi
des autres mesures valides. Compréhension non évaluée.
