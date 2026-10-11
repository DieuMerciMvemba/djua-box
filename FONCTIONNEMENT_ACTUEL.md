# Fonctionnement actuel de l’IoT Box V2

Mise à jour : 11 octobre 2026. La branche `codex/refonte-firmware-iot-box-v2`
implémente uniquement le matériel V2 ; la validation physique reste à réaliser.
Les nouveaux capteurs, le modem et l’émission D3 sont désactivés par défaut
jusqu’à leur confirmation. Aucun profil de matériel alternatif n'est conservé.

## Architecture

```text
INA228 BAT 0x40 ───────────┐
INA228 SOL 0x41 ───────────┤
INA3221 0x42 DC1/DC2/DC3 ──┼─ I²C partagé GPIO21/22 ─┐
INA228 TV 0x44 ────────────┘                         │
PZEM-004T V3 10 A ── UART logiciel RX35/TX14 ───────┤
NEO-8M ──────────── UART2 RX16/TX17 ────────────────┼─ ESP32
DS1302 ──────────── ThreeWire 26/25/27 ─────────────┤  │
Console USB ─────── UART0 GPIO1/3 ──────────────────┘  │
                                    NVS : SMS signés immuables
                                                     │
                                    A7670 UART1 RX32/TX13
                                                     │
                                    SMS D3T × 2 / événements D2E
                                                     │
                                    Récepteur externe à adapter
```

Le brochage, les hypothèses de variante et les risques figurent dans
[la phase 0](docs/REFONTE_PHASE0.md). Le choix de l’UART logiciel du PZEM a été
validé avec l’utilisateur ; EspSoftwareSerial 8.1.0 est installé. Aucun UART
matériel n’est partagé. GPIO14 ne peut pas simultanément piloter PWRKEY.

## Responsabilités

- `iot_box.ino` lance directement `src/firmware_v2.cpp`, qui orchestre la V2.
- `src/drivers` contient le bus I²C partagé, INA228 multi-instance, INA3221
  trois voies et PZEM Modbus/CRC. Ces pilotes ont un cœur testable sans ESP32.
- `src/config.h` centralise résistances, calibres, limites, polarités, adresses,
  confirmations matérielles, temporisations et activation du récepteur D3.
- `src/location/location.h` choisit une source indépendante de la technologie :
  NEO-8M principal, fournisseur cellulaire optionnel injecté. Le GNSS modem
  n’est jamais utilisé. Le cellulaire est exclu de la geofence.
- `src/telemetry` représente séparément BAT/SOL/AC/DC1/DC2/DC3/TV et la qualité,
  intègre les énergies DC/batterie/solaire par trapèzes et l’AC par différence
  de compteur matériel. Un trou ou un reset invalide la complétude.
- `src/communication` encode, authentifie, persiste et retransmet les SMS.
  Le moteur A7670 vérifie réseau et configuration SMS séparément.
- `src/status` et `src/indicators` conservent la décision d’alerte et les LEDs/buzzer.
- `api` reçoit les JSON MQTT de test et les conserve en mémoire ; il ne constitue
  pas le récepteur SMS externe. Son stockage reste volatil.

## Flux principal

Démarrage : console/indicateurs → Wire une seule fois → capteurs confirmés →
GPS indépendant → DS1302 → journal SMS et modem confirmé (ou Wi-Fi en test).

La boucle alimente GPS, PZEM et AT sans attendre la fin d’une conversion capteur.
Toutes les 10 s, elle déclenche les INA en single-shot puis collecte leurs
résultats et défauts. Les registres restent figés après cette conversion, ce qui
évite de relier V et I de deux conversions différentes. La geofence se vérifie
à sa cadence de 5 s. Le PZEM est interrogé toutes les 2 s.

Acquisitions → validité/âge → fenêtres par voie → clôture → modèle V3 → deux
SMS D3T signés → écriture atomique NVS → scheduler → A7670 → opérateur.
La période SMS configurée dans le code actuel est **60 s** ; 1800 s se choisit
explicitement avec `D2_PERIOD_SECONDS`.

La partie 0 transporte batterie, solaire, AC et localisation ; la partie 1
transporte DC1/DC2/DC3/TV. Aucun port USB n’est mesuré. Chaque SMS est au maximum
149 septets. Chaque partie a sa propre séquence et son HMAC ; snapshot relie
les deux. [Le contrat D3](docs/SMS_D3.md) décrit bits, unités, plages et assemblage.

## Fiabilité et sécurité

La NVS `djua_d2` et ses structures restent compatibles. Les anciens D2T/D2T2/D2E
sont relus selon leur version puis retransmis sans réencodage. Les séquences
sont réservées avant emploi et les retries conservent le même texte signé.
La file contient 16 SMS, dont 4 périodiques : deux fenêtres D3 au maximum.
Une saturation refuse explicitement la nouvelle fenêtre.

HMAC-SHA256 authentifie les octets ; il ne chiffre pas les mesures. La clé
propre à l’appareil reste provisionnée localement hors du dépôt. Le modem ne
retire un SMS de la file qu’après `+CMGS` puis `OK` ; cela ne prouve pas réception
ou traitement par le récepteur. Un enregistrement LTE seul ne prouve pas SMS.

Position invalide, précision inconnue et énergie incomplète sont explicites.
En JSON, les valeurs absentes deviennent null. En SMS, elles occupent des zéros
avec flags éteints ; un vrai zéro conserve valid=1. Le compteur PZEM a une
résolution physique de 1 Wh ; aucune énergie plus fine n’est inventée.
Le DS1302 conserve son comportement GMT+1, sans heure de compilation fiable en
production.

## Profils et vérification

SMS_PRODUCTION, MQTT_TEST et HTTP_TEST restent exclusifs. Les profils de test
sérialisent un JSON V3 commun incluant quatre dc_ports ; aucune implémentation
d’un récepteur externe n’a été modifiée. Son adaptation reste nécessaire.

Les simulations testent les registres, CRC, timeouts, réponses AT, localisation,
HMAC, payloads, reprises NVS, absences et compteurs. Les compilations ne flashent
pas l’ESP32. Procédure réelle et sources officielles autorisées :
[validation V2](docs/VALIDATION_V2.md). Le compte rendu des résultats se trouve
[dans la livraison](docs/LIVRAISON_V2.md).
