# Audit de refonte IoT Box — phase 0

Date : 11 octobre 2026. Périmètre : ce dépôt uniquement. Branche dédiée :
`codex/refonte-firmware-iot-box-v2`. État initial Git propre.

## Architecture observée et incompatibilités

`iot_box.ino` orchestre les capteurs, les fenêtres énergétiques, la geofence,
les indicateurs et les transports exclusifs. La batterie initialise actuellement
Wire : ce couplage sera remplacé par un gestionnaire I²C commun.

| Fonction | Existant | Cible | Changement / risque |
| --- | --- | --- | --- |
| Batterie | INA219 0x40, calibre fixe 2 A | INA228 0x40, shunt 30 A/75 mV | Calibration indépendante, courant signé |
| Solaire | INA219 0x41, désactivé | INA228 0x41, shunt 30 A/75 mV | Activation explicite et résistance indépendante |
| Charges DC | Un INA219 0x44 non configuré | INA3221 0x42, trois voies + INA228 TV 0x44 | Identités DC1/DC2/DC3/TV ; aucun port USB |
| AC | ADC ZMPT101B/ACS712, acquisition bloquante | PZEM-004T 10 A | Modbus/CRC, compteur matériel, fréquence/PF ; pas de Q signée ni d'harmoniques |
| Modem | SIM800C UART1 | A7670 UART1 | Enregistrement LTE distinct de capacité SMS ; variante de carte à confirmer |
| Position | NEO-6M UART2/TinyGPSPlus | NEO-8M UART2 | Service de localisation, source/âge/précision ; cellulaire optionnel |
| Temps | DS1302 local GMT+1 | Conservé | Epoch UTC seulement si date valide ; aucune heure fictive |
| Télémétrie | D2T2 : une charge DC | Contrat versionné proposé | Le récepteur externe ne comprend pas encore le nouveau contrat |
| Persistance | NVS `djua_d2`, SMS signé immuable | Conservée | Relire D2T/D2T2/D2E sans changement de sens ni effacement |

Les trois UART matériels sont occupés : UART0 console, UART1 modem, UART2 GPS.
Un quatrième périphérique ne peut pas recevoir un de ces UART sans arbitrage.
Après échange avec l'utilisateur, choix cible : UART logiciel à 9600 bauds pour
le PZEM avec EspSoftwareSerial, UART matériels indépendants conservés. L'utilisateur
a autorisé cette bibliothèque et les documentations officielles des composants.
Le pont SC16IS750 était une alternative ; il n'est pas requis par cette solution.

## Brochage cible

| Liaison | Broches ESP32 | Connexion / contrôle nécessaire |
| --- | --- | --- |
| I²C partagé | SDA21, SCL22 | INA228 BAT 0x40, SOL 0x41, TV 0x44 ; INA3221 0x42 ; straps à contrôler |
| A7670 UART1 | RX32, TX13 | TX modem vers RX32, RX modem vers TX13 ; niveaux et alimentation de la carte à confirmer |
| NEO-8M UART2 | RX16, TX17 | TX GPS vers RX16, RX GPS vers TX17 ; ESP32-WROOM présumé, WROVER/PSRAM à vérifier |
| PZEM UART logiciel | RX35, TX14 | TX PZEM vers RX35 ; RX PZEM vers TX14 ; GPIO35 entrée seule ; adaptation des niveaux requise selon carte |
| DS1302 | DATA26, CLK25, CE27 | Conservé |
| Indicateurs | LEDs18/19/23, buzzer33 | Conservés |
| Console / programmation | UART0 GPIO1/3 | Conservée et distincte du PZEM |

GPIO34 de l'ancien ZMPT devient libre. GPIO14 ne peut pas simultanément servir
de PWRKEY : la configuration devra détecter ce conflit. Les limites électriques,
la protection secteur et les isolations se vérifient sur le montage réel.

## Shunts et mesures

Le shunt 30 A/75 mV vaut **0,0025 Ω**. À 30 A, il dissipe **2,25 W** et
présente 75 mV. Chaque INA228 reçoit sa propre résistance, son courant maximal,
son sens et son calibre ADC. Une plage shunt inférieure à 75 mV ne convient pas
au courant nominal. Les valeurs de registres et limites doivent être vérifiées
dans la documentation officielle avant activation.

Chaque shunt INA3221 vaut **0,010 Ω**. Sa chute et sa dissipation sont `I × R`
et `I² × R`. La mission ne fournit pas la puissance admissible de ces shunts
ni le courant nominal des ports : les limites logicielles devront être explicites,
et leur validation matérielle restera obligatoire.

Les énergies DC et batterie restent des intégrales signées de mesures valides.
Un trou d'acquisition rend la fenêtre incomplète. Pour le PZEM, une baisse du
compteur indique une remise à zéro ou un défaut : pas de delta négatif inventé.
Un compteur ne garantit pas, à lui seul, que la puissance/fenêtre est valide.

## Décisions protocolaires

Ne pas réutiliser les bits réservés D2T2 pour changer silencieusement son sens.
Comparer un SMS unique (perte de précision/champs) à plusieurs SMS indépendants
signés. Proposition : deux SMS GSM-7 versionnés liés par un identifiant de fenêtre,
avec numéros de séquence distincts, chacun vérifié avant persistance. Le récepteur
doit exposer explicitement une réception partielle ; ni l'envoi ni `+CMGS` ne
prouvent la réception de la paire. Une paire consomme deux places dans la file.

Le stockage conserve les mêmes structures de SMS et leur CRC. La création d'une
paire doit être atomique côté NVS. Les anciens textes continuent à être décodés
selon leur préfixe puis retransmis sans réencodage. Le déploiement d'un nouveau
contrat nécessite l'adaptation du récepteur, livrée ici sous forme de spécification.

## Risques et validations restantes

- Variante A7670, tensions UART, alimentation, baud et PWRKEY inconnus :
  configuration explicite ; ne pas réutiliser la validation du SIM800C.
- Version PZEM réellement 10 A, alimentation de son interface et niveaux UART :
  à vérifier. Tester les CRC, timeouts et pertes sous trafic GPS/LTE simultané.
- INA228 : mesurer les résistances réelles, contrôler straps, saturation,
  calibration, signe et dissipation ; aucun test simulé ne prouve la précision.
- INA3221 : vérifier l'absence de shunts embarqués supplémentaires et la puissance
  des trois 10 mΩ. Deux circuits physiques à la même adresse ne se distinguent
  pas par un scanner ; les straps doivent être contrôlés avant connexion commune.
- NEO-8M : confirmer la référence physique et les phrases NMEA ; HDOP n'est pas
  une précision en mètres. La localisation cellulaire ne déclenchera pas la geofence.
- Récepteur absent : aucun fichier ni contrat externe modifié, aucun succès
  d'envoi/réception affirmé.

## Référence de tests avant changements

Exécutés : `python tests/sms/run_tests.py` (7 tests Python, 8 groupes C++ PASS)
et `python tests/dc_load/run_tests.py` (4 variantes PASS). Ces résultats couvrent
l'ancien firmware ; ils ne valident aucun nouveau composant matériel.

## Lots suivants

1. Bus partagé, interface de mesure et pilotes INA3221/INA228, tests de registres.
2. Pilote PZEM, UART logiciel et suivi du compteur d'énergie.
3. A7670, réseau LTE/SMS, service de localisation NEO-8M, bancs simulés.
4. Modèle multi-voies, contrat versionné, encodeurs et file atomique compatible.
5. Intégration, compilations de profils, régressions et validation réelle documentée.
