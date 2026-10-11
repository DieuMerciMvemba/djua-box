# Banc de conformité SMS et HMAC avec A7670

Le sketch autonome
[d2_esp32_hmac_test.ino](d2_esp32_hmac_test/d2_esp32_hmac_test.ino)
utilise les vrais codecs, le journal NVS et le moteur AT.
Son vecteur D2T2 permet de vérifier les messages persistés ; la télémétrie
de production utilise [D3](../../docs/SMS_D3.md).

## Configuration

- A7670 : UART1 RX32/TX13 à 115200 bauds ; vérifier variante, alimentation
  et niveaux électriques avant raccordement.
- Le modem est allumé indépendamment ; ce banc ne suppose aucun PWRKEY.
- Console USB : 115200 bauds, LF ou CRLF.
- NVS dédiée : `djua_d2_bench`, distincte de la file de production.
- Aucun SMS automatique au boot. `SEND` et `RETRY` sont des commandes manuelles.
- Les données du vecteur sont synthétiques ; aucun capteur n'est lu.

Garder le sketch dans ce dépôt : les fichiers C++ incluent le code par
chemins relatifs. Un téléversement remplace le programme de l'ESP32.
Conserver la NVS et désactiver tout effacement complet.

## Clé locale et commandes

Au démarrage, attendre `SELFTEST_OK` puis `MODEM_READY`.
La clé publique de conformité sert à l'autotest RAM et n'est pas provisionnée.

Fermer le moniteur série avant d'utiliser `provision_bench.py` avec le port
USB ESP32 et le fichier de configuration local du récepteur. Consulter
`python "TEST UNITAIRE/SMS/provision_bench.py" --help` pour ses arguments.
Le script vérifie l'identité du banc avant transfert, ne montre pas la clé
et n'envoie aucun SMS. Il ne remplace pas une identité déjà provisionnée.
Résultat attendu : `KEY_OK`.

Le script cible exclusivement l'identité de banc `DJUA-TEST-20261006-A`.
Ne pas publier la configuration privée ni les échanges de provisionnement.

Rouvrir le moniteur puis, volontairement :

```text
BENCH STATUS
SEND
```

`SEND` crée au plus un message par démarrage, avec mesures absentes et durée
de test déclarée. `SEND VECTOR` crée un vecteur synthétique, pas une fenêtre
de mesure réelle. Les séquences sont réservées durablement : un saut après
redémarrage est attendu, aucune remise à zéro automatique.

`+CMGS` puis `OK` signifie acceptation modem. Après erreur ou timeout,
le texte persiste et `RETRY` le retransmet identique. `CANCEL` ne supprime
pas la file. Au récepteur, contrôler signature, séquence et données ; la
livraison ne se déduit pas du seul acquittement modem.

## Vérifications sans téléversement

```powershell
python -m unittest discover -s "TEST UNITAIRE/SMS" -p test_provision_bench.py -v
python tests/sms/run_tests.py
```

La compilation Arduino cible `esp32:esp32:esp32`.
Les tests simulés ne prouvent ni le fonctionnement de l'opérateur ni
l'atomicité d'une coupure réelle d'alimentation flash.
