# Essai SMS du firmware avec A7670

Ce guide concerne `iot_box.ino`, l'A7670 sur UART1 et les capteurs V2.
Aucun téléversement ou SMS réel n'est déclenché par les tests automatisés.

## Avant l'essai

1. Confirmer la variante A7670, son alimentation, les niveaux UART et le baud.
2. Câbler TX modem → RX ESP32 GPIO32 et RX modem → TX GPIO13 ; masse commune.
   UART1 est configuré à 115200 bauds. GPS reste sur UART2 GPIO16/17.
3. Contrôler PWRKEY : GPIO14 est réservé au PZEM. Ne renseigner qu'une autre
   broche dont le circuit de commande est validé.
4. Utiliser d'abord [le banc AT](../TEST%20UNITAIRE/A7670/a7670_at_test/a7670_at_test.ino)
   pour relever `ATI`, `AT+CPIN?`, `AT+CEREG?`, `AT+CREG?`,
   `AT+CSMS?` et `AT+CSCA?`.
5. Activer `GSM_HARDWARE_CONFIGURED` après ces contrôles. Confirmer les
   capteurs individuellement selon [VALIDATION_V2.md](VALIDATION_V2.md).

## Identité et transmission

La NVS `djua_d2` contient l'identité, la clé HMAC, le destinataire, la réserve
de séquences et la file. Provisionner seulement une NVS vierge avec une
identité autorisée au récepteur ; conserver les secrets hors du dépôt.
La console de provisionnement est temporaire et doit être désactivée après usage.

Ne pas effacer une file déjà présente. Les textes signés en attente sont
retransmis tels quels. Une nouvelle acquisition utilise le modèle V3.

Adapter le récepteur au [contrat D3](SMS_D3.md), puis activer
`D3_RECEIVER_ENABLED`. La partie 0 transporte BAT/SOL/AC/localisation ;
la partie 1 transporte DC1/DC2/DC3/TV.

## Critères d'acceptation

- SIM prête, enregistrement réseau, SMS MO disponible et SMSC valide.
- Prompt `>`, puis `+CMGS` et `OK` pour chaque SMS.
- Au récepteur : deux séquences, HMAC valides, snapshot commun,
  données et complétude correspondant aux mesures.
- Après timeout ou reboot : même texte, même séquence et même signature
  pour le retry ; aucune moitié de paire créée dans le journal.
- En absence de mesure : valeurs absentes explicites, aucun zéro fabriqué.

L'acceptation par le modem ne prouve pas la livraison. L'alimentation,
l'opérateur et l'assemblage au récepteur restent à vérifier physiquement.
