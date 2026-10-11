# Troisième INA219 : charges DC avec shunt externe

Le pilote mesure la tension côté charge (VIN−), le courant signé et la puissance
`Vcharge × I`. Le firmware intègre ensuite l'énergie en Wh sur la période D2T2.
Le solaire et la batterie gardent leurs pilotes et calibrations actuels.

## Configuration

Dans `src/config.h` :

| Paramètre | Valeur livrée | Action |
| --- | --- | --- |
| `DC_LOAD_MEASUREMENT_ENABLED` | 1 | 0 pour désactiver la fonction |
| `DC_LOAD_SHUNT_CONFIGURED` | 0 | 1 seulement après choix et montage du shunt |
| `DC_LOAD_INA219_I2C_ADDRESS` | `0x44` | Proposition à vérifier avec le scanner |
| `DC_LOAD_SHUNT_RATED_A` | 50.0 | Remplacer par le courant nominal réel |
| `DC_LOAD_SHUNT_RATED_MV` | 75.0 | Remplacer par la chute nominale réelle |
| `DC_LOAD_MAX_VALID_V` | 26.0 | Abaisser selon le circuit ; jamais dépasser 26 V |
| `DC_LOAD_CURRENT_POLARITY` | 1 | −1 si l'orientation du shunt l'exige |

Les valeurs 50 A / 75 mV sont un **exemple logiciel**, pas un shunt matériel choisi.
Toutes les constantes sont communes au `.ino` et aux `.cpp`. Une définition seulement
dans le sketch ne configure pas le pilote compilé séparément.

Pour 50 A / 75 mV : `Rshunt = 0.075 / 50 = 0.0015 Ω` ; calibration `0x4000`,
résolution courant d'environ 1.667 mA. Le calibre nominal est rejeté en cas de
dépassement ; aucune saturation silencieuse. Ces calculs ne dimensionnent ni les
câbles, ni les fusibles, ni le shunt. Sa dissipation nominale est `50 × 0.075 = 3.75 W`.

## Montage à vérifier

- I²C partagé : SDA21, SCL22, masse commune ; alimentation logique/pull-ups
  compatibles 3,3 V ESP32.
- `0x40` est réservé à la batterie, `0x41` au solaire ; `0x44` correspond à A1=VS,
  A0=GND selon TI. Vérifier les ponts disponibles sur la carte réelle.
- Placer le shunt externe dans le conducteur positif alimentant **les charges DC**.
  Les fils de mesure VIN+ et VIN− vont aux points de mesure du shunt (connexion Kelvin
  si disponible). Le retour des charges reste sur le négatif commun.
- Le courant de puissance passe dans le shunt/câblage externe, pas dans les pistes
  ou bornes d'un petit module INA219 standard. Vérifier l'adaptation/retrait du shunt
  embarqué : ne pas supposer qu'un module R100 est déjà prévu pour ce montage.
- Positif = consommation DC ; négatif = retour vers la source.
- L'INA219 est limité à 26 V sur ses entrées malgré le nom de plage numérique
  « 32 V ». Le contrat D2T2 jusqu'à 100 V ne change pas cette limite physique.

Référence primaire : [fiche TI INA219, adressage, registres et calibration](https://www.ti.com/lit/ds/symlink/ina219.pdf).

## Essais

1. Configurer l'adresse matérielle, puis ouvrir le scanner existant
   `TEST UNITAIRE/INA219_SOLAR/i2c_scanner` : il affiche désormais les trois adresses.
2. Choisir le shunt, vérifier câblage/calibres et reporter ses valeurs dans `config.h`.
3. Mettre `DC_LOAD_SHUNT_CONFIGURED=1`, puis téléverser manuellement le sketch
   `ina219_dc_test`. Aucune commande automatique de flash n'est fournie.
4. Vérifier à charge coupée puis avec une charge connue, en comparant au multimètre.
5. Déconnecter/reconnecter le capteur : mesure invalide puis récupération, sans
   reconfiguration globale de Wire et sans perturber les adresses batterie/solaire.
6. Dans le firmware complet, une panne DC persistante produit WARNING après trois
   acquisitions. L'alerte geofence et les défauts critiques restent prioritaires.

Tests automatiques sans matériel : `python tests/dc_load/run_tests.py`.
Ils couvrent calibration, registres, signe, médiane, saturation, défauts, récupération,
télémétrie, alertes et absence d'accès I²C quand la mesure est désactivée/non configurée.
Ils ne prouvent ni la précision du montage réel ni sa capacité à transporter 50 A.
