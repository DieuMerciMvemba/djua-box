# Progression

## Concepts compris

Aucun concept n'est considéré comme maîtrisé sans vérification de la
compréhension de l'utilisateur.

## En apprentissage

- Architecture matérielle unique : `iot_box.ino` lance le firmware V2 ;
  les profils SMS/MQTT/HTTP choisissent uniquement le transport.
- I²C partagé : trois INA228 et un INA3221 sur quatre adresses distinctes.
- Shunt et calibration : résistance réelle, sens du courant et puissance
  admissible déterminent les paramètres de chaque voie.
- UART matériel et logiciel : console, A7670 et NEO-8M ont leurs UART dédiés ;
  EspSoftwareSerial sert au PZEM à 9600 bauds.
- Validité et complétude : mesure instantanée correcte et énergie de fenêtre
  complète sont deux informations différentes.
- NVS, séquences et retries : un SMS signé persiste et se retransmet identique.
- HMAC et livraison : authentifier un message ne le chiffre pas et ne garantit
  pas sa réception.
- Localisation : source, âge, HDOP et précision métrique inconnue restent distincts.
- RTC : date locale GMT+1 et epoch UTC ; une heure de compilation ne suffit
  pas à valider une horloge réelle.

## Nouveaux concepts

- Suppression d'un pilote : retirer les macros ne suffit pas ; il faut retirer
  les appels, les structures de mesure, les bancs et les guides qui en dépendent.
- Modèle V3 : DC1/DC2/DC3/TV gardent leur identité dans le JSON et dans D3.
- Insertion atomique : les deux SMS d'une fenêtre entrent ensemble dans la NVS.

## À revoir

- Rôle d'un `#define` : il configure le programme compilé ; il n'atteste pas
  que le composant est physiquement présent et validé.
- Pourquoi un scanner I²C ne détecte pas deux circuits à la même adresse.
- Différence entre tests de bus simulé, compilation ESP32 et essai matériel.
- Pourquoi les messages stockés se lisent selon leur version, indépendamment
  du choix des capteurs du firmware.
