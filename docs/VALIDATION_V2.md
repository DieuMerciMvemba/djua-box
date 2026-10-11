# Validation réelle de la refonte V2

Les tests PC et une compilation ne prouvent ni précision, ni isolation,
ni capacité SMS opérateur. Aucun téléversement ni envoi réel n'est effectué par
les scripts de tests. Ne pas effacer la NVS lors d'une mise à jour.

## Configuration et câblage

Voir [audit et tableau de brochage](REFONTE_PHASE0.md), [contrat D3](SMS_D3.md).
Le firmware compile uniquement les pilotes V2, quel que soit le transport.
EspSoftwareSerial **8.1.0** est installé ; Arduino ESP32 local **3.3.12** et
ArduinoJson **7** sont utilisés. Console UART0, A7670 UART1 RX32/TX13, NEO-8M
UART2 RX16/TX17 ; PZEM UART logiciel RX35/TX14 à 9600 8N1. Aucune interface
matérielle n'est attribuée à deux périphériques. GPIO35 n'a pas de pull-up
interne : contrôler le niveau de repos fourni par l'interface/adaptateur.
Sur un autre poste, installer **EspSoftwareSerial 8.1.0** dans le gestionnaire
de bibliothèques Arduino avant compilation ; les sources de cette dépendance
ne sont pas recopiées dans le dépôt.

Tous les `V2_*_CONFIGURED`, `PZEM_HARDWARE_CONFIGURED` et
`GSM_HARDWARE_CONFIGURED` valent zéro par défaut : les variantes physiques ne sont
pas encore validées. Les activer individuellement dans `src/config.h` après
contrôle. `D3_RECEIVER_ENABLED=0` empêche la création des nouveaux SMS D3 tant
que le récepteur externe n'est pas adapté. Une ancienne file signée peut continuer
à être drainée si le modem a été configuré ; ne pas remettre les séquences à 1.

Les paramètres BAT/SOL/TV définissent chacun résistance réelle, courant maximal,
puissance admissible, tension de bus maximale, polarité et plage ADC.
Les résistances nominales 30 A/75 mV sont 2,5 mΩ ; à 30 A, dissipation 2,25 W.
Les 3 W configurés sont un minimum provisoire à vérifier sur chaque shunt.
Les ports DC1/DC2/DC3 ont chacun R=10 mΩ, une limite provisoire de 5 A et une
puissance minimale provisoire de 1 W. À 5 A : chute 50 mV, dissipation 0,25 W.
Ces paramètres ne constituent pas une validation de la capacité physique.

## 1. I²C et shunts — sans charge, puis charge de référence

1. Déconnecter les périphériques puis connecter chacun séparément. Vérifier les
   straps : BAT 0x40, SOL 0x41, INA3221 0x42, TV 0x44. Utiliser le scanner
   `TEST UNITAIRE/I2C/i2c_scanner` pour ce contrôle.
2. Ne connecter ensemble que des adresses différentes. Le firmware refuse
   les conflits de configuration ; un scanner ne peut pas distinguer deux circuits
   physiques répondant à la même adresse.
3. Vérifier/remplacer les shunts éventuellement intégrés sur les modules pour
   obtenir la résistance effective prévue. Préférer les connexions Kelvin.
4. Activer une voie à la fois, ouvrir le moniteur et relever `valid`, `fault`,
   V, I, P, ainsi que les registres/calibration si nécessaire. Pour 30 A/75 mV,
   plage ±163,84 mV, SHUNT_CAL attendu 1875 ; avec une résistance différente,
   recalcul automatique. La plage ±40,96 mV est refusée au calibre 30 A.
5. Comparer tension/courant à des instruments de référence à zéro puis plusieurs
   niveaux ; vérifier charge positive et décharge négative de la batterie.
6. Charger séparément DC1/DC2/DC3/TV et vérifier que seule la voie attendue varie.
   Aucun port USB n'entre dans ce modèle. Vérifier températures et chutes de tension.
7. Débrancher un capteur ; constater son défaut, les autres voies maintenues et
   l'énergie concernée incomplète. Rebrancher : récupération au cycle suivant,
   énergie encore incomplète jusqu'à la fenêtre suivante.

## 2. PZEM-004T **V3.0, 10 A à shunt intégré**

La mission ne précise pas la révision protocolaire : ce pilote exige V3.0/Modbus,
pas l'ancien protocole V1/V2 ni la version CT 100 A. Confirmer le marquage physique.
Contrôler alimentation de l'interface, niveaux UART, adaptation vers 3,3 V et
isolation/protections secteur avant connexion à l'ESP32.

1. Activer seulement `PZEM_HARDWARE_CONFIGURED`. Adresse configurable : 0xF8
   pour un seul module, ou son adresse réelle 0x01..0xF7.
2. Comparer V, I, puissance active, compteur Wh, PF et fréquence à une référence.
   S=V×I est calculée ; aucune Q signée ni mesure harmonique n'est fournie.
3. Déconnecter TX : timeout/énergie incomplète. Reconnecter : reprise des lectures,
   sans déclarer l'intervalle troué complet.
4. Garder NEO-8M actif et provoquer trafic A7670 pendant au moins une fenêtre
   complète. Vérifier absence de pertes UART, CRC et débordements. La fiabilité
   de l'Uart logiciel sous cette charge reste un essai matériel obligatoire.
5. Une remise à zéro volontaire du compteur se fait uniquement dans une procédure
   de maintenance explicite (commande Modbus 0x42 sur un banc séparé). Le firmware
   n'envoie jamais cette commande automatiquement. Un compteur qui baisse rend
   l'énergie de la fenêtre absente, jamais négative. Un saut implausible est rejeté.
6. Vérifier la résolution 1 Wh : les faibles consommations peuvent produire un
   delta zéro réel pendant une courte fenêtre ; la dernière puissance n'est pas
   utilisée pour inventer une énergie plus fine.

## 3. NEO-8M et DS1302

1. Modem éteint, vérifier les phrases NMEA à 9600 bauds et une position extérieure.
   L'étiquette NEO-8M ne vérifie pas la référence réelle ; confirmer la variante.
2. Retirer le signal GPS : au-delà de 5 s la localisation est invalide, pas un
   emplacement fictif. Le service conserve source et âge. HDOP n'est pas accuracy_m.
3. La précision métrique reste inconnue avec les seules phrases NMEA décodées.
   Si une précision métrique fournie par un futur pilote est mauvaise, la geofence
   la refuse. Toute position cellulaire est refusée pour la geofence locale.
4. Vérifier les transitions GNSS et trois confirmations ; un manque de position
   casse la série de confirmations, sans changer le dernier état confirmé.
5. Vérifier l'heure DS1302 locale GMT+1 et sa conversion UTC. Enlever la pile ou
   provoquer une date invalide. En SMS production, l'initialisation à la date de
   compilation est désactivée. Les profils de test MQTT/HTTP ont
   `RTC_AUTO_INITIALIZE_IF_INVALID=1` : le forcer à **0** pour ce contrôle réel.
   Une heure de compilation utilisée au banc ne constitue pas une heure réelle validée.

## 4. A7670, réseau et SMS

1. Confirmer variante de carte, alimentation, niveaux UART, baud, logique PWRKEY
   et comportement après reset ESP32 avec modem déjà alimenté. PWRKEY GPIO14 est interdit
   car occupé par le PZEM ; un GPIO distinct confirmé est nécessaire.
2. Banc manuel : `TEST UNITAIRE/A7670/a7670_at_test`. Il n'envoie aucun SMS
   automatiquement. Moniteur avec fin CR : `AT`, `ATI`, `AT+CGMR`, `AT+CPIN?`,
   `AT+CEREG?`, `AT+CREG?`, `AT+CSMS?`, `AT+CSCA?`.
3. Enregistrement LTE 1/5 et service SMS MO=1/SMSC configuré permettent d'essayer
   l'envoi ; ils ne prouvent pas son fonctionnement opérateur. Aucune commande
   GNSS intégré ni appel automatique au service LBS n'est envoyé.
4. Provisionner uniquement si `djua_d2` est vierge, avec la vraie identité/clé
   déjà autorisée au récepteur. Garder clé/numéro privés hors du dépôt. La console
   locale est temporaire ; recompiler ensuite sans provisionnement.
5. Adapter le récepteur au [contrat D3](SMS_D3.md), puis seulement activer D3.
   Vérifier prompt `>`, `+CMGS`, `OK`, réception des deux séquences, HMAC, assemblage,
   données et complétude au récepteur. `+CMGS` seul ne prouve pas la réception.
6. Retirer la SIM, perdre le réseau, injecter erreur CMS, redémarrer entre les
   parties et remplir la file : aucune moitié créée en NVS, retries immuables,
   refus explicite de saturation, priorités d'urgence conservées.

## 5. Tests et limites

```powershell
python tests/refonte/run_tests.py
python tests/sms/run_tests.py
python -m unittest discover -s "TEST UNITAIRE/SMS" -p test_provision_bench.py -v
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/sms/compile_profiles.ps1
# Depuis le dossier api, dans l'environnement du dépôt :
& ./.venv/Scripts/python.exe -m unittest discover -s tests -v
```

Les profils MQTT_TEST/HTTP_TEST sérialisent le JSON V3 : quatre `dc_ports`,
diagnostics, énergies complètes ou null et précision inconnue null. Ils restent
exclusifs du transport SMS et utilisent la même architecture matérielle.
Aucun backend HTTP externe n'a été modifié :
sa prise en charge du nouveau JSON doit être validée séparément avant usage réel.

Reste à valider au matériel : exactitude, thermiques, protections/isolations,
adresses/straps réels, UART logiciel sous charge, fix GNSS, RTC, alimentation et
SMS opérateur, réception/assemblage externe. Le fournisseur cellulaire optionnel
est une interface injectée, pas une localisation inventée ni un service LBS actif.

## Sources primaires consultées avec autorisation

- [TI INA228](https://www.ti.com/lit/ds/symlink/ina228.pdf) : registres,
  plages ADC, calibration et signes.
- [TI INA3221](https://www.ti.com/lit/ds/symlink/ina3221.pdf) : registres,
  conversion et identités.
- [Manuel PZEM V3.0 du fabricant](https://innovatorsguru.com/wp-content/uploads/2019/06/PZEM-004T-V3.0-Datasheet-User-Manual.pdf).
- [SIMCom A7670 et documentation A76XX](https://en.simcom.com/product/A7670X.html),
  [manuel SIMCom A76XX V1.04, copie du document fabricant](https://m2mserver.com/m2m-downloads/A76XX_Series_AT%20Command%20Manual_V104.pdf).
- [EspSoftwareSerial, auteur de la bibliothèque](https://github.com/plerup/espsoftwareserial).
