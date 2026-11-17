# Format D2T2 des SMS périodiques

Le firmware utilise D2T2 pour envoyer toute la télémétrie périodique dans un
seul SMS. Les textes D3T déjà présents dans la file NVS restent lisibles et
retransmis sans modification ; aucune nouvelle fenêtre périodique D3T n'est
créée.

## Trame

```text
D2T2,DEV,PAYLOAD,AUTH
```

PAYLOAD contient 60 octets, soit 480 bits MSB d'abord, encodés en 80 caractères
Base64URL sans padding. AUTH contient les 8 premiers octets du HMAC-SHA256,
encodés en 11 caractères Base64URL. Le HMAC signe les octets ASCII exacts
`D2T2,DEV,PAYLOAD`. Avec un identifiant d'appareil de 32 caractères, la trame
fait au plus 130 septets GSM-7 ; elle tient dans un SMS de 160 septets.

Le codec refuse les valeurs hors plage, les bits réservés non nuls, les absences
non canoniques et les dépendances de flags incorrectes.

## Groupes de présence

| Bit | Groupe |
| --- | --- |
| 0 | RTC |
| 1 | Latitude / longitude |
| 2 | Batterie V/I/P |
| 3 | Solaire V/I/P |
| 4 | AC V/I/P/S |
| 5 | Énergie solaire, exige bit 3 |
| 6 | Énergie AC, exige bit 4 |
| 7 | Groupe DC V/I/P |
| 8 | Énergie DC, exige bit 7 |

Les bits 9..15 du champ flags restent nuls. Après les flags, les trois premiers
bits finaux encodent `measured_count` (0..4) ; les trois derniers sont réservés
et doivent rester nuls. `measured_count` est supérieur à zéro exactement quand
le groupe DC V/I/P est présent. Un courant mesuré à zéro conserve donc le groupe
DC et compte comme une mesure.

Le groupe DC contient la moyenne arithmétique de la tension, du courant et de la
puissance des canaux valides parmi DC1, DC2, DC3 et TV. `measured_count` indique
le nombre de canaux inclus ; aucune mesure individuelle n'est transmise. Les
pilotes INA3221 et INA228 lisent tous la tension de bus côté charge : la moyenne
est celle des tensions mesurées aux bornes des canaux, pas une estimation d'un
bus électrique commun.

Les anciennes trames D2T2 avaient leurs six bits finaux à zéro. Elles restent
décodables. Si le groupe DC est présent sans compteur, le décodeur expose le
compteur comme inconnu au lieu d'inventer une valeur.

Un champ absent occupe zéro avec son flag éteint ; un zéro réellement mesuré
conserve son flag.

## Référence et reprise

Implémentation : `src/communication/d2t2_codec.cpp`.
Fixtures : [tests/sms/d2t2_conformance.json](../tests/sms/d2t2_conformance.json).
Vérification : `python tests/sms/run_tests.py`.

Le journal [SMS_D2.md](SMS_D2.md) conserve les textes signés sans les convertir
vers un autre format. Le compteur de séquence et les retries restent communs
aux événements et aux SMS périodiques.

Le [banc A7670/HMAC](../TEST%20UNITAIRE/SMS/README.md) peut envoyer manuellement
un vecteur public synthétique. Ce banc vérifie le contrat et le transport ; il
ne réalise aucune acquisition électrique.
