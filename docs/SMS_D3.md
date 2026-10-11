# Contrat proposé D3T — IoT Box V2

Version indépendante de D2T/D2T2. Récepteur externe absent du dépôt : cette
spécification décrit le travail nécessaire côté récepteur ; aucun récepteur
n'a été modifié ni déclaré compatible. Émission D3 à activer explicitement après
son adaptation. Les événements de geofence GNSS conservent D2E.

## Compactage et répartition

Une seule trame texte ne tient pas avec quatre charges DC, leurs énergies,
les diagnostics, le PF et la fréquence. Étendre D2T2 casserait son contrat.
Un SMS binaire ou concaténé impose un autre mode modem et un assemblage opérateur.
Choix : deux SMS texte indépendants GSM-7, Base64URL sans padding, HMAC par SMS.
La consommation SMS double ; la précision est conservée. Une seule partie reçue
reste utile mais la fenêtre complète n'est pas déclarée reçue.

```text
D3T,DEV,PAYLOAD,AUTH
```

DEV suit les règles D2, 1..32 caractères. PAYLOAD : exactement 75 octets, 100
caractères Base64URL (`A-Z a-z 0-9 - _`), 600 bits, MSB d'abord. Tous ces caractères
occupent un septet. Longueur authentifiée = `117 + longueur(DEV)`, soit **149 maximum**.
AUTH : 11 caractères Base64URL des 8 premiers octets de HMAC-SHA256, clé 32 octets
propre à l'appareil. Signature des octets ASCII exacts `D3T,DEV,PAYLOAD`, sans
virgule finale. AUTH `-` uniquement pour les tests de développement explicitement
non authentifiés ; interdit dans la file de production. HMAC ne chiffre pas.

Les nombres sont arrondis à mi-chemin en s'éloignant de zéro, les nombres signés
utilisent le complément à deux. Une valeur hors plage fait refuser la création,
sans clamp. Les valeurs absentes doivent avoir leurs emplacements à zéro.

## En-tête commun — bits 0..146

| Champ | Bits | Sens |
| --- | ---: | --- |
| sequence | 32 | uint32 non nul, compteur durable commun aux anciens SMS et D2E |
| snapshot | 32 | sequence de la partie 0, 1..UINT32_MAX-1 |
| rtc | 32 | epoch UTC ; 0 si rtc_valid=0 ; sinon années 2000..2099 |
| uptime | 32 | millis au moment de clôture, wrap permis |
| interval | 17 | durée réelle, 10..86400 secondes |
| part | 1 | 0 : système ; 1 : les quatre charges DC |
| rtc_valid | 1 | fiabilité de rtc |

`sequence = snapshot + part`. Les deux parties partagent DEV, snapshot, rtc,
rtc_valid, uptime et interval. Un redémarrage conserve les textes signés.

## Enregistrement électrique DC — 103 bits

Utilisé pour batterie et solaire dans la partie 0 et les quatre charges dans la
partie 1. Ordre exact :

| Champ | Bits | Unité / plage |
| --- | ---: | --- |
| valid | 1 | grandeur instantanée valide |
| energy_complete | 1 | toutes les acquisitions nécessaires de la fenêtre valides |
| fault | 4 | code ci-dessous |
| age_ms | 16 | âge ; 65535 signifie au moins 65535 ms |
| voltage | 17 | mV unsigned, 0..85000 |
| current | 17 | mA signed, -30000..30000 |
| power | 20 | 0,1 W signed, -30000..30000 unités |
| energy | 27 | 0,01 Wh signed ; valeur absolue <= 3000 W × interval/3600 + 1 Wh |

`valid` exige `fault=0`. Une mesure invalide exige un défaut non nul et V/I/P=0.
`energy_complete` exige valid ; sinon energy=0. Un zéro avec valid=1 reste une
mesure zéro, pas une absence. Les unités et plages du protocole ne valident pas
le dimensionnement physique du montage.
Pour une voie DC valide, `|P - V×I| <= 1 W + 2 % × |P|` ; cette tolérance
accepte la quantification des champs sans accepter une puissance incohérente.

## Partie 0 — ordre après l'en-tête

1. Localisation (87 bits) : source(2), valid(1), accuracy_known(1), age_ms(16),
   accuracy_m(16 unsigned, 0..65534 m), latitude(25 signed, 1e-5°),
   longitude(26 signed, 1e-5°). Source 0=aucune, 1=NEO GNSS, 2=cellulaire,
   3 interdit. LAT -90..90°, LON -180..180°. Coordonnées nulles si invalide ;
   précision nulle si inconnue. HDOP reste une information de diagnostic local,
   jamais transformée artificiellement en mètres. Une source peut être connue
   alors que sa position est périmée/invalide.
2. Batterie (103 bits), courant positif=charge, négatif=décharge.
3. Solaire (103 bits), sens selon configuration.
4. AC PZEM (152 bits), ordre de la table suivante.
5. 8 bits réservés à zéro, bits 592..599.

| Champ AC | Bits | Unité / sens |
| --- | ---: | --- |
| valid, energy_complete, counter_valid, alarm | 1 chacun | quatre flags indépendants et cohérents |
| fault, energy_fault | 4 chacun | acquisition / compteur-fenêtre |
| age_ms | 16 | 65535 = âge saturé |
| voltage | 12 | 0,1 V, 0..260 V |
| current | 14 | 0,001 A, 0..10 A |
| active_power | 15 | 0,1 W, 0..2300 W |
| apparent_power | 15 | 0,1 VA, 0..2600 VA, calculée V×I |
| energy_interval | 27 | 0,01 Wh, unsigned ; <= 2300 W × interval/3600 + 1 Wh |
| counter | 24 | compteur absolu matériel, Wh, 0..9999990 |
| frequency | 10 | 0,1 Hz, 0..65 Hz |
| power_factor | 7 | 0,01, 0..1 |

Une AC invalide met V/I/P/S/f/PF/alarm à zéro. counter_valid exige valid et,
s'il est faux, counter=0. energy_complete exige valid, counter_valid et
energy_fault=0. Si incomplète, energy_interval=0. P ne doit pas dépasser
S + 2 W + 3 % (tolérance aux résolutions). Ni puissance réactive signée ni
harmoniques dans ce contrat. Le compteur PZEM a une résolution physique de 1 Wh,
même si le champ d'intervalle commun permet 0,01 Wh.

## Partie 1

Après l'en-tête : DC1, DC2, DC3, TV, chacun 103 bits. Identités fixes ; aucun
port USB. Les 41 bits 559..599 sont réservés et doivent être zéro.

## Défauts

0=aucun ; 1=désactivé/non confirmé ; 2=configuration ; 3=conflit d'adresse ;
4=I/O ; 5=mauvais composant ; 6=conversion non prête ; 7=overflow ;
8=hors plage ; 9=timeout/âge/trou ; 10=CRC ; 11=protocole ; 12=reset compteur.
13..15 interdits. Une énergie DC incomplète peut coexister avec une mesure
instantanée correcte ; energy_complete=0 conserve cette distinction.

## Persistance et travail côté récepteur

Le firmware conserve namespace `djua_d2`, formats et CRC des blobs identity,
sequence, queue. Une paire est créée par une seule écriture de queue après
réservation durable des deux séquences. En cas d'erreur, aucune moitié ajoutée.
Capacité : 16 SMS, dont au maximum 4 périodiques, donc 2 fenêtres D3 ; 12 places
restent réservées aux urgences. La saturation refuse explicitement la nouvelle
fenêtre. Chaque retry conserve exactement le texte, la séquence et le HMAC.
Retrait après `+CMGS` et `OK` uniquement ; réception finale non confirmée.
Cette compatibilité concerne les anciens messages relus par le nouveau firmware.
Un binaire antérieur à D3 peut refuser une file qui contient D3 ; conserver les
blobs et utiliser un firmware comprenant D3 pour la reprendre, sans effacement NVS.

Le récepteur doit :

- continuer à interpréter D2T/D2T2/D2E avec leurs anciens contrats ;
- reconnaître D3T et refuser longueurs, réserves, flags, absences, plages et
  relations de séquence invalides ; vérifier HMAC et liaison numéro GSM/appareil ;
- dédupliquer par (DEV, sequence) en conservant sa politique anti-rejeu durable ;
- assembler par (DEV, snapshot), accepter les parties dans n'importe quel ordre,
  comparer leurs en-têtes communs et signaler toute divergence ;
- gérer réception partielle/expiration sans fabriquer les voies manquantes ;
- exposer source, âge, précision inconnue, diagnostics, complétude et `dc_ports`
  identifiés séparément. Garder la précision inconnue comme null ;
- tenir compte de la quantification physique du compteur AC ; ne pas dériver
  une remise à zéro en consommation négative ;
- ne déclarer une fenêtre complète que lorsque les deux parties authentifiées
  ont été reçues et leurs en-têtes comparés. Cela ne rend pas les énergies
  automatiquement complètes : leurs flags restent nécessaires.

Tests reproductibles : `python tests/refonte/run_tests.py`. Le vecteur utilise
uniquement une clé publique de test 01..20 hex, jamais une clé terrain. La
référence Python vérifie indépendamment les deux payloads et leurs HMAC.
