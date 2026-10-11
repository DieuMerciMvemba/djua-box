# Vérification des codecs et de la file SMS

Les codecs de messages persistés sont indépendants du choix des capteurs.
Les nouvelles acquisitions utilisent V3 et D3.

## Tests reproductibles

À la racine du dépôt :

```powershell
python tests/sms/run_tests.py
python tests/refonte/run_tests.py
python -m unittest discover -s "TEST UNITAIRE/SMS" -p test_provision_bench.py -v
```

Les suites vérifient :

- D2T/D2T2/D2E : vecteurs publics, unités, flags, HMAC, absences et vrais zéros ;
- D3 : deux payloads de 75 octets confrontés à une référence Python indépendante ;
- NVS : corruption, lectures de versions, reboot, retries immuables et séquences ;
- insertion atomique de la paire, refus de saturation et fin du compteur ;
- A7670 : LTE, service SMS, SMSC, erreurs et timeouts simulés ;
- provisionnement USB : identification du banc avant transfert de clé.

Les résultats de la version courante sont dans [LIVRAISON_V2.md](LIVRAISON_V2.md).
Les essais physiques et opérateur sont dans [VALIDATION_V2.md](VALIDATION_V2.md).
Une simulation ou compilation ne prouve pas une réception réelle.
