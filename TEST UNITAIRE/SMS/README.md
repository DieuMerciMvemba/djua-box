# Test D2T2 : signature calculee directement par ESP32 / SIM800C

Ce test autonome utilise le vrai codec D2, le journal NVS et le moteur AT de
`src/communication`. Il ne modifie pas `iot_box.ino`, `config.h`, DJUA_SMS ou D2.
Reference DJUA_SMS : `c5835077b2b6e8418e34898013cacc0892c12e25`.
Le banc utilise maintenant D2T2 ; aucune NVS n'est effacee et le compteur existant
est conserve. Un ancien SMS D2T en attente reste immutable lors de RETRY.

## Portee et precautions

- Emetteur : SIM800C deja teste a 9600 bauds. Recepteur : SIM868 / DJUA_SMS.
- ESP32 UART1 RX=32, TX=13 : TXD SIM800C vers GPIO32, RXD SIM800C vers GPIO13,
  GND commun. Le premier essai valide utilisait 16/17 ; ces broches restent
  reservees au GPS dans le firmware complet. Ce sketch teste uniquement le GSM.
- Le modem doit deja etre alimente/allume. Aucun PWRKEY ni alimentation supposee.
- USB/moniteur ESP32 : 115200 bauds, fin de ligne LF ou CRLF.
- Pas de Wi-Fi, MQTT, HTTP, GPRS, APN, faux capteurs ou horloge inventee.
- Un `SEND` cree au plus un D2T2 par demarrage. Aucun SMS automatique au boot,
  aucune relance automatique d'un SMS en attente. `RETRY` est une action explicite.
- Namespace NVS **`djua_d2_bench`**, distinct de `djua_d2` et de la calibration AC.
  Pas de commande d'effacement/reprovisionnement : ne pas effacer la flash/NVS.
- NVS/USB ne sont pas chiffres. La cle reste sensible, meme pour ce banc.
  Ne pas publier le fichier gateway.env, la commande de cle, une capture ou un log USB.
- Televerser remplace le programme actuellement execute sur l'ESP32. Cela n'efface
  pas volontairement la NVS ; desactiver l'option Arduino d'effacement complet.

## 1. Ouvrir et televerser (manuel)

Ouvrir `d2_esp32_hmac_test/d2_esp32_hmac_test.ino` dans Arduino IDE,
ESP32 Dev Module et **port USB de l'ESP32**, pas COM16 (recepteur).
Garder le sketch dans ce depot : `core.cpp` inclut les vrais fichiers par chemins
relatifs. Ne pas copier seulement le .ino dans un autre dossier.

Au boot, l'autotest RAM utilise la cle publique officielle de conformite,
compare le vecteur D2T2 authentifie exact (dont mesures DC) et refuse un HMAC altere. Cette cle publique
n'est ni provisionnee ni envoyee. On attend `SELFTEST_OK`, puis `MODEM_READY`.
Ces messages sont a verifier sur le materiel, la compilation seule ne les prouve pas.

## 2. Importer la cle privee locale par USB

DJUA_SMS peut continuer de fonctionner en arriere-plan sur son port COM16.
**Fermer le moniteur serie Arduino** avant le transfert USB.

Dans CMD (adapter COM_ESP32 au port affiche par Arduino IDE) :

```cmd
cd /d C:\Users\Dell\Desktop\DJUA_SMS
.venv\Scripts\python.exe "C:\Users\Dell\Downloads\iot_box\TEST UNITAIRE\SMS\provision_bench.py" --port COM_ESP32
```

Le script lit exclusivement la cle de `DJUA-TEST-20261006-A` dans le fichier local
`config/gateway.env`, verifie le dialogue du nouveau sketch avant transfert, refuse
le port du recepteur et ne montre pas la cle. Il n'ecrit aucun fichier et n'envoie
aucun SMS. Une NVS deja provisionnee avec une autre cle/identite est refusee.
Resultat attendu : `KEY_OK`. Ne pas changer la cle du recepteur pour contourner un refus.

## 3. Envoyer depuis l'ESP32

Rouvrir le moniteur serie : 115200 / Nouvelle ligne. Cette ouverture peut rebooter
la carte ; la cle reste en NVS. Attendre `MODEM_READY`, puis :

```text
BENCH STATUS
SEND
```

On attend `D2_READY`, `CRYPTO=OK`, `MODEM=READY`, puis `SUBMIT sequence=5`
sur un premier banc vierge. ESP32 construit D2T2 et calcule HMAC via mbedTLS.
Le journal reserve les sequences par blocs de 256 avant emploi : apres usage puis
reboot, 261 ou un nombre superieur peut etre normal. La sequence exacte est affichee.
Elle ne recommence jamais automatiquement a 5. Les sequences 1..4 precedentes
restent reservees aux essais manuels deja realises.

Le D2T2 declare l'intervalle de test 1800 s avec toutes les mesures absentes et flags
binaires zero ; ce n'est pas une acquisition reelle de 30 minutes. `uptime_ms` vient de millis()
et ne sera plus forcement zero. Ni RTC, ni position, ni puissance n'est inventee.

Pour tester le transport du groupe DC avec des donnees **synthetiques**, utiliser
`SEND VECTOR` a la place de `SEND` : 12.35 V / 3.2 A / 39.5 W / 19.76 Wh.
Le message affiche SYNTHETIC_VECTOR, utilise l'identite/cle privee du banc,
une sequence neuve et l'uptime reel. RTC/GPS/autres valeurs de la fixture restent
synthetiques. Ce test ne valide pas le shunt ni les acquisitions physiques.

`+CMGS` puis `OK` donnent `MODEM_ACCEPTED` : acceptance modem, pas livraison backend.
Apres echec/timeout, le meme SMS reste en NVS. Attendre modem pret puis `RETRY`
pour un retry volontaire identique, sans nouveau message_id. `CANCEL` n'interrompt
pas une soumission en cours et ne supprime jamais la file persistante.

## 4. Verification bout en bout

L'API locale doit etre en fonctionnement et abonnee avant SEND :

```cmd
cd /d C:\Users\Dell\Downloads\iot_box\api
uv run uvicorn app.main:app --reload --host 127.0.0.1 --port 8000
```

Verifier <http://127.0.0.1:8000/devices/DJUA-TEST-20261006-A/telemetry/latest> :

- `data.protocol=D2T2`, `data.auth_status=VERIFIED` ;
- `data.sequence` = sequence affichee par ESP32 ;
- message_id coherent, nouvelles dates gateway/API ;
- mesures null, validity=false ; timestamp/timezone absents.

Pour `SEND VECTOR`, verifier plutot les valeurs DC ci-dessus et les flags valides.
Les deux formes partagent la limite d'un nouveau message par demarrage.

L'exterieur `sequence` appartient a l'API, pas au compteur D2.
Garder les logs de l'ESP32 et du recepteur sans cle privee.

## Verifications reproductibles, aucun flash automatique

```cmd
cd /d C:\Users\Dell\Downloads\iot_box
python "TEST UNITAIRE\SMS\test_provision_bench.py"
python tests\sms\run_tests.py
```

Compilation PowerShell (ou utiliser Arduino IDE) :

```powershell
& "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" compile --fqbn esp32:esp32:esp32 --warnings all --jobs 1 "TEST UNITAIRE\SMS\d2_esp32_hmac_test"
```

Tests du banc : configuration locale, format de cle, refus cle publique, dialogue
USB, refus d'un ancien sketch, absence d'envoi de cle avant identification, aucune
commande SEND par l'importateur. Tests existants : vrai codec, journal, moteur AT.
Ils ne prouvent pas le resultat sur SIM800C ni l'atomicite d'une coupure flash reelle.

Prochaine phase distincte : D2E simule et signe. Integration des capteurs, choix des
GPIO definitifs, association numero/appareil et securisation MQTT restent a valider.
