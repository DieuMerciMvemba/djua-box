# Format D2T2 des SMS persistés

Le firmware de production crée désormais des paires [D3T](SMS_D3.md).
D2T2 reste disponible pour relire les textes signés en NVS et pour les
vecteurs de conformité. Il ne sélectionne aucun pilote de capteur.

## Trame

```text
D2T2,DEV,PAYLOAD,AUTH
```

PAYLOAD contient 60 octets, 480 bits MSB d'abord, encodés en 80 caractères
Base64URL sans padding. AUTH est une signature HMAC-SHA256 tronquée à 8 octets,
encodée en 11 caractères. Les octets ASCII exacts `D2T2,DEV,PAYLOAD` sont signés.
Une identité de 32 caractères produit au maximum 130 septets GSM-7.

Le codec refuse les valeurs hors plage, bits réservés non nuls, absences
non canoniques et dépendances de flags incorrectes.

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

Les bits 9..15 du champ flags et les six bits finaux du payload restent zéro.
Un champ absent occupe zéro avec son flag éteint. Un zéro réel conserve son flag.
Le groupe DC de ce contrat ne représente pas les quatre identités DC1/DC2/DC3/TV :
celles-ci sont transportées par D3.

## Référence et reprise

Implémentation : `src/communication/d2t2_codec.cpp`.
Fixtures indépendantes :
[tests/sms/d2t2_conformance.json](../tests/sms/d2t2_conformance.json).
Vérification : `python tests/sms/run_tests.py` et
`python tests/refonte/run_tests.py`.

Le journal [SMS_D2.md](SMS_D2.md) conserve les textes signés sans les convertir
vers un autre format. Le compteur et la politique de retry restent communs
aux événements et aux messages périodiques.

Le [banc A7670/HMAC](../TEST%20UNITAIRE/SMS/README.md) peut envoyer manuellement
un vecteur public synthétique. Ce banc teste le contrat et le transport ;
il ne réalise aucune acquisition électrique.
