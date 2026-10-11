# Contrat de données DJUA IoT → DJUA_SMS → backend — D2

Mise à jour firmware du 7 octobre 2026 : la nouvelle télémétrie est **D2T2**,
avec le groupe `dc_load` (voltage_v, current_a, power_w, energy_interval_wh),
les flags DC valide/énergie complète et les mêmes topics/message_id.
Voir [contrat et intégration D2T2](SMS_D2T2.md). D2E reste inchangé.
Le contrat D2T historique décrit ci-dessous reste accepté côté récepteur.

Version documentaire : 1. Date : 5 octobre 2026.

## 1. Périmètre et statut

Ce document décrit le JSON **réellement produit par DJUA_SMS pour D2**, puis les
exigences que le backend doit respecter pour le consommer. Il ne change ni le
protocole SMS, ni le code du récepteur, ni le code du backend.

Référence figée du récepteur :
`toussaintmadimba-cmyk/DJUA_SMS`, SHA
`aac1101176e241a9e6ad2b1f5ec404adb3069a28`.

Sources vérifiées à ce SHA :

- [Codec et normalisation D2](https://github.com/toussaintmadimba-cmyk/DJUA_SMS/blob/aac1101176e241a9e6ad2b1f5ec404adb3069a28/src/djua_sms_gateway/protocol/d2.py).
- [Ingestion et création de l'outbox](https://github.com/toussaintmadimba-cmyk/DJUA_SMS/blob/aac1101176e241a9e6ad2b1f5ec404adb3069a28/src/djua_sms_gateway/services/ingestion.py).
- [Publication et PUBACK](https://github.com/toussaintmadimba-cmyk/DJUA_SMS/blob/aac1101176e241a9e6ad2b1f5ec404adb3069a28/src/djua_sms_gateway/mqtt/publisher.py).
- [Spécification SMS D2](https://github.com/toussaintmadimba-cmyk/DJUA_SMS/blob/aac1101176e241a9e6ad2b1f5ec404adb3069a28/docs/sms_protocol_v2.md).
- [Contrat historique D1 et extension D2 du récepteur](https://github.com/toussaintmadimba-cmyk/DJUA_SMS/blob/aac1101176e241a9e6ad2b1f5ec404adb3069a28/docs/backend_data_contract.md).
- [Vecteurs officiels](../tests/sms/d2_conformance.json), copiés depuis cette référence.

**Statut : contrat D2 documenté ; compatibilité du backend réel non validée.**
L'API locale `api/app/main.py` est une API de test : elle conserve les objets
JSON reçus en mémoire, mais n'implémente pas encore la validation D2,
l'idempotence durable ni le stockage de production décrits ci-dessous.

## 2. Architecture et responsabilités

```text
Capteurs / geofence
→ ESP32 : mesures, fenêtre énergétique, séquence, HMAC, file NVS
→ SMS D2T ou D2E
→ passerelle SMS / DJUA_SMS : validation, HMAC, association expéditeur/appareil
→ SQLite : ingestion durable, déduplication, outbox
→ MQTT : JSON normalisé
→ backend : validation JSON, idempotence, persistance, traitements métier
```

En production, la box utilise uniquement le SMS. HTTP et MQTT directs sont
des profils de test séparés, pas des secours automatiques de production.

Le backend ne reçoit pas le SMS brut et ne redécode pas les nombres base36.
Les nombres JSON sont déjà exprimés dans les unités physiques ci-dessous.

## 3. Transport MQTT

Préfixe de compatibilité : `djua/test`. Il est configurable dans DJUA_SMS ;
les deux côtés doivent utiliser exactement le même préfixe et le même broker.
La casse des topics est significative.

| Message | Topic par défaut | Sens |
| --- | --- | --- |
| `D2T` | `djua/test/<kit_id>/telemetry` | Mesures et énergies d'une période |
| `D2E` | `djua/test/<kit_id>/geofence/events` | Transition geofence confirmée |

Valeurs de référence du récepteur : **QoS 1, retain=false**.
Le backend de production doit s'abonner avec QoS 1 ; un abonnement QoS 0
abaisse la garantie sur le trajet broker → consommateur.

QoS 1 permet des doublons ; il ne prouve pas le traitement métier.
La continuité pendant une panne du backend exige aussi une configuration
adaptée du broker et de la session abonnée : une outbox du récepteur ne remplace
pas une souscription durable du backend. Cette configuration reste à valider.

Payload : objet JSON UTF-8, nombres finis uniquement, aucun `NaN`/`Infinity`.
L'ordre des clés et les espaces JSON ne font pas partie du contrat.

## 4. Champs communs obligatoires D2T / D2E

| Champ | Type | Règle |
| --- | --- | --- |
| `protocol` | string | Exactement `D2T` ou `D2E`, cohérent avec le topic |
| `message_id` | string | `D2:<kit_id>:<séquence_base36>` |
| `sequence` | integer | 1 à 4294967295 ; partagé entre D2T et D2E |
| `kit_id` | string | Identité technique, format `[A-Z0-9-]{1,32}` ; égale à celle du topic |
| `uptime_ms` | integer | 0 à 4294967295 ; temps depuis le démarrage ESP32, pas Unix |
| `gateway_received_at` | string | ISO 8601 UTC de première ingestion durable par DJUA_SMS |
| `validity` | object | Booléens définis par le type de message, tous obligatoires |
| `auth_status` | string | `VERIFIED` en production ; `NOT_VERIFIED` réservé au développement |

`sequence` est numérique dans le JSON ; son suffixe dans `message_id` est en
base36 majuscule canonique, sans zéro initial. Exemple : `12345` → `9IX`.
Le préfixe de l'identifiant reste `D2`, y compris pour D2T et D2E.

La séquence ne recommence pas après un reboot et ne doit jamais boucler.
Des trous sont normaux : réservation par blocs de 256, messages refusés ou
perdus. Une priorité d'envoi ou un retard réseau peut inverser l'ordre d'arrivée.
Le backend ne doit pas rejeter un message inédit parce que sa séquence est
inférieure au dernier message reçu.

Après perte irrécupérable du compteur ou remplacement sans migration sûre,
une nouvelle identité technique est nécessaire. Le backend peut la rattacher
au même kit commercial ; `kit_id` n'est pas une preuve d'identité commerciale.

## 5. Temps et horodatage

Lorsque `validity.rtc=true`, les deux champs suivants sont obligatoires :

| Champ | Type | Exemple |
| --- | --- | --- |
| `timestamp` | string ISO 8601 | `2026-09-27T16:30:00+01:00` |
| `timezone` | string | `GMT+1` |

Lorsque `validity.rtc=false`, **les deux champs sont absents**, pas présents à
`null`. Le backend ne doit pas inventer une date de mesure à partir de l'uptime.

Le RTC du SMS est exprimé en secondes Unix UTC, dans la plage
2000-01-01T00:00:00Z à 2099-12-31T23:59:59Z. DJUA_SMS le présente en GMT+1.
L'extrémité supérieure peut donc être affichée en 2100 côté heure locale.

`gateway_received_at` est une date du récepteur, non signée par l'ESP32.
Elle reste inchangée pendant les relectures et republications du même message.
Un éventuel `backend_received_at` est une métadonnée propre au backend,
pas un champ actuellement émis par DJUA_SMS.

Pour D2T seulement, `timestamp_ms` est obligatoire et exactement égal à
`uptime_ms`. Ce nom historique ne désigne **jamais** une date Unix.
Uptime peut repartir de zéro après reboot et boucler ; il n'est pas une clé
d'identification ni une horloge de classement globale.

## 6. Schéma D2T : mesures périodiques

Cadence nominale actuelle : 1800 secondes, soit 30 minutes. Le backend utilise
`interval_seconds` reçu, pas une constante supposée. La première période doit
être acquise avant la création du premier D2T.

Tous les objets et champs suivants sont obligatoires. Les mesures sont des
`number` JSON ou `null` ; les plages s'appliquent aux valeurs numériques.

| Champ | Unité / résolution D2 | Plage |
| --- | --- | --- |
| `interval_seconds` | integer, secondes réellement couvertes, arrondies | 10 à 86400 |
| `latitude` | degré / 0,00001° | −90 à +90 |
| `longitude` | degré / 0,00001° | −180 à +180 |
| `battery.voltage_v` | V / 0,001 V | 0 à 32 |
| `battery.current_a` | A / 0,001 A | −200 à +200 |
| `battery.power_w` | W / 0,1 W | −6400 à +6400 |
| `solar.voltage_v` | V / 0,001 V | 0 à 100 |
| `solar.current_a` | A / 0,001 A | −100 à +100 |
| `solar.power_w` | W / 0,1 W | −10000 à +10000 |
| `solar.energy_interval_wh` | Wh / 0,01 Wh | −240000 à +240000 |
| `ac_load.voltage_v` | V RMS / 0,1 V | 0 à 400 |
| `ac_load.current_a` | A RMS / 0,01 A | 0 à 30 |
| `ac_load.active_power_w` | W / 0,1 W | −12000 à +12000 |
| `ac_load.apparent_power_va` | VA / 0,1 VA | 0 à 12000 |
| `ac_load.energy_interval_wh` | Wh / 0,01 Wh | −288000 à +288000 |

Ces plages sont celles de représentation du protocole, **pas les limites
électriques des capteurs**, ni une preuve de calibration.
Les résolutions ne garantissent pas la précision physique des mesures.
Les calculs du backend doivent tolérer la représentation décimale des nombres
flottants ; ne pas exiger une égalité binaire exacte entre `V × I` et `P` arrondi.

### Validité et absence

`validity` contient exactement les booléens actuellement émis suivants :

| Booléen | Champs associés |
| --- | --- |
| `rtc` | Présence conjointe de `timestamp` et `timezone` |
| `gps` | `latitude`, `longitude` |
| `battery` | Les trois mesures `battery.*` |
| `solar` | `solar.voltage_v`, `solar.current_a`, `solar.power_w` |
| `solar_energy` | `solar.energy_interval_wh` |
| `ac` | Les quatre mesures instantanées `ac_load.*`, hors énergie |
| `ac_energy` | `ac_load.energy_interval_wh` |

Pour les mesures : booléen `false` → tous les champs associés sont `null` ;
booléen `true` → tous sont numériques, finis et dans la plage.
Une valeur numérique zéro est une vraie mesure, jamais un marqueur d'absence.

`solar_energy=true` exige `solar=true` ; `ac_energy=true` exige `ac=true`.
La réciproque est fausse : les dernières mesures peuvent être valides alors que
l'énergie de toute la période est incomplète. L'énergie reste alors `null`.

### Énergies et signes

- Batterie : courant/puissance positifs = charge ; négatifs = décharge.
- Solaire : puissance/énergie positives = production vers le système ;
  négatives = flux inverse.
- AC : puissance active/énergie positives = consommation de la charge ;
  négatives = retour/export vers la source.

Ne pas remplacer les valeurs négatives par leur valeur absolue et ne pas
assimiler une énergie nette signée à deux compteurs import/export séparés.
Aucune énergie batterie n'est transmise dans D2T.

Les tensions, courants et puissances sont les dernières acquisitions valides
et suffisamment récentes à la fermeture de la fenêtre, **pas des moyennes de
30 minutes**. Les énergies sont des intégrales nettes de période, pas des
compteurs cumulés à vie et pas `dernière puissance × intervalle`.

Politique firmware : acquisition toutes les 10 s ; intervalle admissible entre
points ≤20 s ; fraîcheur électrique ≤20 s ; fraîcheur GPS ≤5 s.
Un point invalide ou un trou >20 s rend l'énergie de la période incomplète.
L'intégration utilise les trapèzes ; la mesure frontière termine une période
et commence la suivante sans compter deux fois le même segment.

Le backend doit conserver les `null` et n'additionner qu'une fois chaque énergie
valide, après déduplication. Il doit signaler une couverture incomplète plutôt
que présenter la somme des seules périodes reçues comme une énergie totale sûre.

### Exemple D2T officiel

```json
{
  "protocol": "D2T",
  "message_id": "D2:DJUA-KIN-000001:9IX",
  "sequence": 12345,
  "kit_id": "DJUA-KIN-000001",
  "uptime_ms": 123456789,
  "timestamp_ms": 123456789,
  "timestamp": "2026-09-27T16:30:00+01:00",
  "timezone": "GMT+1",
  "gateway_received_at": "2026-09-27T15:30:05.123000+00:00",
  "interval_seconds": 1800,
  "latitude": -4.3251,
  "longitude": 15.3222,
  "battery": {"voltage_v": 12.4, "current_a": 0.5, "power_w": 6.2},
  "solar": {"voltage_v": 18.2, "current_a": 1.23, "power_w": 22.4, "energy_interval_wh": 11.2},
  "ac_load": {"voltage_v": 230.1, "current_a": 1.25, "active_power_w": 244.5, "apparent_power_va": 287.6, "energy_interval_wh": 122.25},
  "validity": {"rtc": true, "gps": true, "battery": true, "solar": true, "solar_energy": true, "ac": true, "ac_energy": true},
  "auth_status": "VERIFIED"
}
```

## 7. Schéma D2E : événements geofence

D2E est émis sur transition confirmée, sans cadence périodique imposée.
Il ne transporte **pas** les mesures batterie/solaire/AC ni les énergies.
Les seuls événements actuels sont :

| Code SMS | `event` JSON | `state` JSON |
| --- | --- | --- |
| `GX` | `GEOFENCE_EXIT` | `OUTSIDE` |
| `GE` | `GEOFENCE_ENTER` | `INSIDE` |

`state` est déduit du code par DJUA_SMS. Il ne désigne pas un état GPS recalculé
à réception du MQTT. Une urgence batterie ou panne capteur n'est pas un nouveau
type D2E supporté : elle nécessiterait une évolution explicite du protocole.

En plus des champs communs et des deux champs temporels conditionnels :

| Champ obligatoire | Type / règle |
| --- | --- |
| `event` | string, une des deux valeurs ci-dessus |
| `state` | string, cohérent avec `event` |
| `position_usable` | boolean, exactement égal à `validity.gps` |
| `latitude`, `longitude` | number ou null, mêmes plages/résolutions que D2T |
| `distance_m` | integer de 0 à 1000000, ou null |
| `validity.rtc` | boolean ; même règle de présence temporelle que D2T |
| `validity.gps` | boolean ; false → coordonnées null, true → coordonnées numériques |
| `validity.distance` | boolean ; false → distance null, true → distance numérique et GPS valide |

`distance_m` est la distance géodésique entière au **centre** de la zone,
pas la distance à sa frontière. Centre, rayons, état candidat et nombre de
confirmations ne sont pas transmis. Le backend ne doit pas inventer ces champs
ni recalculer la transition en utilisant un rayon supposé.

D2E n'émet pas `timestamp_ms` ni `interval_seconds`.
Le récepteur ne publie pas de snapshot D2 sur `.../geofence`.
L'absence d'événement n'est pas une preuve que le boîtier est actuellement dedans :
SMS retardé/perdu, file pleine, reboot ou état initial inconnu sont possibles.

### Exemple D2E officiel

```json
{
  "protocol": "D2E",
  "message_id": "D2:DJUA-KIN-000001:9IY",
  "sequence": 12346,
  "kit_id": "DJUA-KIN-000001",
  "uptime_ms": 123460000,
  "timestamp": "2026-09-27T16:30:00+01:00",
  "timezone": "GMT+1",
  "gateway_received_at": "2026-09-27T15:30:07.000000+00:00",
  "event": "GEOFENCE_EXIT",
  "state": "OUTSIDE",
  "position_usable": true,
  "latitude": -4.3251,
  "longitude": 15.3222,
  "distance_m": 95,
  "validity": {"rtc": true, "gps": true, "distance": true},
  "auth_status": "VERIFIED"
}
```

## 8. Idempotence et persistance : exigences backend

Ces exigences sont à implémenter/vérifier côté backend ; elles ne sont pas
présentées comme déjà réalisées dans l'API locale.

1. Valider le message et la cohérence topic/identité/protocole.
2. Persister avec une contrainte d'unicité durable sur `message_id`.
3. Même identifiant et même contenu JSON, indépendamment de l'ordre des clés :
   doublon sans nouvel enregistrement métier, alerte ou addition d'énergie.
4. Même identifiant mais contenu différent : conflit à journaliser/quarantainer ;
   ne pas écraser le message original ni traiter silencieusement le nouveau.
5. Réaliser l'enregistrement et les effets métier de manière atomique, ou avec
   un mécanisme de reprise durable. Une simple collection de clés en RAM ne suffit pas.
6. Accepter les messages inédits arrivés en retard et conserver leur histoire.
   Pour afficher le dernier état d'une même identité technique, ne pas laisser
   un événement plus ancien reçu tardivement remplacer un événement plus récent.

La comparaison porte sur le payload de DJUA_SMS, pas sur les métadonnées locales
de réception du backend. Les booléens JSON sont des booléens, pas des entiers
admis par erreur dans un validateur Python. Les numéros de séquence manquants
ne doivent pas être automatiquement interprétés comme des D2T perdus : D2E et
réservations consomment aussi des séquences.

## 9. Fiabilité : niveaux de confirmation distincts

| Observation | Ce qu'elle confirme | Ce qu'elle ne confirme pas |
| --- | --- | --- |
| Mise en file NVS | SMS créé durablement sur la box | Transmission opérateur |
| `+CMGS` puis `OK` | Acceptation par le modem | Réception passerelle SMS ou backend |
| Commit SQLite DJUA_SMS | Ingestion durable locale | Publication MQTT ou traitement backend |
| `PUBACK` / outbox `PUBLISHED` | Accusé du broker | Persistance/traitement métier backend |
| Commit métier backend | Traitement durable côté backend | Accusé retourné à la box |

Le récepteur stocke avant suppression du SMS modem. Son outbox reprend les
publications non confirmées ; un crash après PUBACK et avant commit peut
produire une republication. Il faut donc une idempotence côté backend.

D2 ne définit ni SMS ACK applicatif ni accusé backend retourné au boîtier.
Ne pas inventer un endpoint, un topic ACK ou une garantie exactement-une-fois.

La file box est bornée à 16 SMS, dont au plus 4 périodiques, pour préserver
12 places aux urgences. Une saturation refuse explicitement les nouveaux
messages ; les périodes énergétiques ne s'accumulent pas indéfiniment.
La réception et le traitement de chaque période ne sont donc pas garantis.

## 10. Authentification et données sensibles

DJUA_SMS vérifie le HMAC du SMS et, en production, l'association du numéro
expéditeur E.164 à l'identité technique. Un SMS rejeté n'engendre pas d'outbox
backend. `VERIFIED` ne signifie pas que les capteurs sont calibrés ni que la
mesure est physiquement correcte : la qualité reste portée par `validity`.

Le JSON ne transporte ni clé HMAC ni tag `AUTH`. Le backend ne peut donc pas
revérifier directement la signature du SMS à partir de ce JSON. `auth_status`
est une assertion du récepteur : le canal gateway → broker → backend doit être
protégé par authentification, TLS et permissions MQTT adaptées en production.
Un broker public de test ne constitue pas cette frontière de confiance.

HMAC authentifie mais ne chiffre pas le SMS. Positions et données temporelles
sont sensibles : limiter l'accès, la conservation et les journaux contenant
des données personnelles. Aucun secret ne doit être ajouté au dépôt.

## 11. Coexistence historique et champs non publiés

Les profils `MQTT_TEST` et `HTTP_TEST` du firmware gardent leur JSON historique.
Ils ne produisent pas ce JSON D2 du récepteur et ne valident pas le parcours SMS.
Le récepteur conserve aussi son chemin D1 historique sans modification.

Ne pas appliquer les anciennes conventions D1 à D2 : coordonnées/batterie/AC
indisponibles deviennent `null` en D2, pas des zéros artificiels.
Le routage D2 s'appuie sur `protocol`. Un JSON sans `protocol` ne doit pas être
considéré implicitement comme D2 ni accepté comme une production D2 authentifiée.
La politique d'acceptation des anciens formats doit être explicite et séparée.

Champs non publiés en D2 : SMS brut, numéro GSM expéditeur, timestamp modem,
tag `auth`, masque brut `flags`, clé secrète, facteur de puissance AC,
énergie apparente en VAh, calibration et paramètres complets de geofence.
Pas de topic artificiel `<kit_id>/status=online` : une box SMS n'est pas un
client MQTT direct. La santé de la gateway relève d'un autre contrat.

La version en tête de ce document n'est pas un champ JSON `schema_version`.
Toute nouvelle règle wire doit être coordonnée entre les deux projets.
Une évolution JSON doit être documentée et testée ; ne pas changer unités,
signification des signes ou convention d'absence silencieusement.

## 12. Critères de validation et écarts actuels

| Contrôle à réaliser | Résultat attendu |
| --- | --- |
| Vecteurs D2T/D2E officiels | Topics et payloads identiques aux références |
| RTC absent | `timestamp` et `timezone` absents, pas de date inventée |
| Mesure invalide / zéro réel | `null` et zéro conservés distinctement |
| Mesures valides, énergie incomplète | Dernières mesures numériques, énergie null |
| Énergies négatives | Signe conservé en stockage et calcul métier |
| Deux livraisons, puis reboot backend | Un seul traitement durable |
| Même identifiant, autre contenu | Conflit signalé, original préservé |
| D2E ancien reçu après D2E récent | Historique conservé, dernier état non rétrogradé |
| Coupure broker/backend | Reprise testée avec configuration de session réelle |
| Production non vérifiée / topic incohérent | Rejet ou quarantaine explicite |
| Vrais SMS, passerelle SMS, underscores des tags | Réception exacte, validation HMAC réussie |

À la rédaction, l'API locale s'abonne en QoS 0, stocke en RAM, n'impose pas
l'unicité de `message_id` et ne valide pas ce schéma D2. Ses tests historiques
ne prouvent pas la conformité de production. Aucun correctif backend n'est
appliqué par la création de ce document.

La documentation d'intégration firmware et les limites matérielles sont dans
[SMS_D2.md](SMS_D2.md). La validation bout en bout reste nécessaire avant terrain.
