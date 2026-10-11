# Architecture et contraintes matérielles IoT Box V2

Mise à jour : 11 octobre 2026. Le firmware possède une seule architecture matérielle.
`iot_box.ino` appelle directement `src/firmware_v2.cpp`. Les profils SMS,
MQTT et HTTP choisissent uniquement le transport.

## Composants et brochage

| Fonction | Composant | Liaison ESP32 |
| --- | --- | --- |
| Batterie | INA228, shunt 30 A/75 mV | I²C 0x40 |
| Solaire | INA228, shunt 30 A/75 mV | I²C 0x41 |
| DC1/DC2/DC3 | INA3221, trois shunts 10 mΩ | I²C 0x42 |
| TV | INA228, shunt 30 A/75 mV | I²C 0x44 |
| AC | PZEM-004T V3.0, 10 A, shunt intégré | UART logiciel RX35/TX14, 9600 bauds |
| SMS | A7670 | UART1 RX32/TX13, 115200 bauds |
| Position principale | NEO-8M | UART2 RX16/TX17, 9600 bauds |
| Horloge | DS1302 | DATA26/CLOCK25/CE27 |
| Indication locale | LEDs et buzzer | LEDs18/19/23, buzzer33 |
| Console USB | UART0 | GPIO1/3 |

Les quatre circuits INA partagent SDA21/SCL22 à 100 kHz. Le gestionnaire
`I2cBus` initialise le bus une seule fois et refuse les conflits d'adresses
configurés. Contrôler les straps séparément : un scanner ne distingue pas
deux circuits physiques répondant à la même adresse.

Le PZEM utilise EspSoftwareSerial 8.1.0, autorisé par l'utilisateur.
Console, modem et GPS conservent des UART matériels indépendants.
GPIO35 est une entrée sans pull-up interne. GPIO14 est réservé au TX du PZEM ;
PWRKEY doit utiliser une autre broche confirmée, ou rester à -1 pour un modem
allumé indépendamment. Confirmer les niveaux électriques de chaque carte.

## Dimensionnement des shunts

Chaque INA228 a sa résistance, sa polarité et ses limites propres dans
`src/config.h`. Un shunt 30 A/75 mV vaut 2,5 mΩ, chute de 75 mV et dissipation
de 2,25 W à 30 A. La plage ±163,84 mV est utilisée ; ±40,96 mV est insuffisante.
La puissance admissible configurée de 3 W reste à confirmer physiquement.

Chaque voie INA3221 utilise 10 mΩ. À la limite provisoire de 5 A : chute de
50 mV et dissipation de 0,25 W. La puissance configurée de 1 W est provisoire.
Vérifier la résistance effective et les shunts éventuellement présents sur
les cartes. Les limites logicielles ne garantissent pas la tenue thermique.

## Flux et persistance

Pilotes → mesures BAT/SOL/AC/DC1/DC2/DC3/TV → validité et âge →
énergie par voie → télémétrie V3 → deux SMS D3 signés → NVS → A7670.

Les acquisitions DC sont déclenchées toutes les 10 s. Le PZEM est interrogé
toutes les 2 s. Les trous de mesure rendent l'énergie de la fenêtre incomplète.
Une baisse du compteur AC ne produit jamais une consommation négative.

Le DS1302 fournit l'heure locale GMT+1, convertie en UTC pour les SMS.
Le NEO-8M fournit la position principale. Une interface cellulaire optionnelle
existe, sans service LBS actif ; une position cellulaire ne déclenche pas
la geofence.

Les deux SMS D3 portent des séquences distinctes et un snapshot commun.
La paire est persistée atomiquement. Le journal `djua_d2` conserve ses
structures et les textes signés déjà enregistrés, avec lecture selon leur
version. Aucun pilote matériel alternatif n'est nécessaire à cette lecture.

## Mise en service

Les confirmations des capteurs et du modem restent à zéro jusqu'aux contrôles
physiques. `D3_RECEIVER_ENABLED=0` bloque les nouvelles paires jusqu'à
adaptation du récepteur externe. Ne pas effacer la NVS ni réinitialiser
les séquences.

Vérifications réelles : [VALIDATION_V2.md](VALIDATION_V2.md).
Contrat du récepteur : [SMS_D3.md](SMS_D3.md).
