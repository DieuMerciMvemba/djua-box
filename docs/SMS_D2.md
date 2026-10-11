# Journal SMS, D2T et D2E

Les acquisitions du firmware utilisent les composants V2 et le modèle V3.
Le format périodique de production est [D3T](SMS_D3.md).
Ce document décrit la persistance et la lecture des messages déjà enregistrés.
D2E reste le format des événements de geofence.

## Contrats conservés

`src/communication/d2_codec.cpp` lit D2T et D2E.
`d2t2_codec.cpp` lit [D2T2](SMS_D2T2.md).
Le préfixe détermine le codec ; le matériel actuellement installé ne change
jamais le sens d'un texte signé en attente.

Les fixtures publiques sont dans
[tests/sms/d2_conformance.json](../tests/sms/d2_conformance.json).
Les règles de validation, flags, unités et HMAC y sont testées par
`python tests/sms/run_tests.py`. Les échanges avec le backend sont décrits
dans [CONTRAT_BACKEND_D2.md](CONTRAT_BACKEND_D2.md).

## Persistance et retransmission

Namespace NVS : `djua_d2`. Les blobs `identity`, `sequence` et `queue`
sont versionnés et protégés contre la corruption.
La NVS conserve l'identité, la clé propre à l'appareil, le destinataire,
la réserve de séquences et les textes signés en attente.

Le compteur durable est réservé par blocs avant emploi. Un redémarrage
peut sauter des numéros ; il ne doit pas réutiliser un numéro.
Une restauration d'une sauvegarde ancienne de NVS ne garantit pas l'anti-rejeu.

Le scheduler envoie via A7670 et retire un SMS seulement après `+CMGS`
puis `OK`. Un timeout laisse le texte inchangé pour le retry.
L'acceptation modem ne prouve pas la livraison finale.
La file compte 16 places, dont 4 périodiques ; une paire D3 consomme deux places.

## Provisionnement local

Un appareil vierge reste `D2_UNPROVISIONED`, sans création ni envoi automatique.
`D2_PROVISIONING_ENABLED=0` par défaut. La console temporaire expose :

```text
D2 STATUS
D2 PROVISION <TECHNICAL_ID> <SECRET_64_HEX> <RECEIVER_E164> <INSTALLATION_REFERENCE> <NEXT_SEQUENCE_DECIMAL>
```

Renseigner une identité et une clé déjà autorisées au récepteur.
Une NVS renseignée refuse le reprovisionnement ; ne pas l'effacer pour
contourner ce refus. Recompiler sans provisionnement après l'installation.

HMAC authentifie les octets sans les chiffrer. La clé privée et le numéro
destinataire restent hors du dépôt. La NVS et la console USB ne sont
pas automatiquement chiffrées.

## Vérification

```powershell
python tests/sms/run_tests.py
python tests/refonte/run_tests.py
```

Ces tests vérifient les codecs, corruption, reprises, séquences, priorités,
paire atomique et retries. Essai opérateur : [TEST_FIRMWARE_GSM.md](TEST_FIRMWARE_GSM.md).
