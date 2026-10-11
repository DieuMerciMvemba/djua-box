# Test et calibration ZMPT101B + ACS712-30A

Ce test mesure simultanement la tension et le courant de la charge AC. Il
affiche la puissance active reelle en W (moyenne de `v(t) * i(t)`), la
puissance apparente en VA et le facteur de puissance `PF = W / VA`.

## Securite et cablage

Les bornes AC peuvent etre a une tension mortelle. Couper et consigner toutes
les alimentations avant intervention, utiliser un boitier isolant et faire
valider le montage par une personne qualifiee.

Partie basse tension :

- ZMPT101B VCC vers 5 V, GND vers GND ;
- ZMPT101B OUT vers une resistance 10 kohms, puis GPIO34 ;
- GPIO34 vers une resistance 10 kohms, puis GND ;
- ACS712-30A VCC vers 5 V, GND vers GND ;
- ACS712 OUT vers une resistance 10 kohms, puis GPIO35 ;
- GPIO35 vers une resistance 10 kohms, puis GND.

Partie puissance : ZMPT101B en parallele sur phase/neutre de la charge ;
ACS712 IP+ puis IP- en serie dans la phase. Ne jamais raccorder la phase ou le
neutre au GND ESP32. Le calibre 30 A est une valeur de crete ; sur un courant
sinusoidal, ne pas depasser environ 21,2 A RMS.

## Procedure de calibration

Ouvrir le moniteur serie a 115200 bauds avec la fin de ligne `Nouvelle ligne`.
Les valeurs sont sauvegardees automatiquement dans la memoire NVS de l'ESP32.

1. Sans courant dans la charge, envoyer `AC CAL ZERO`.
2. Avec une tension stable, envoyer `AC MEASURE`, relever une reference avec
   un multimetre TRUE-RMS, puis envoyer par exemple `AC CAL VOLTAGE 230.0`.
3. Avec une charge stable suffisamment forte, envoyer `AC MEASURE`, relever
   une reference avec une pince TRUE-RMS, puis envoyer par exemple
   `AC CAL CURRENT 2.50`.
4. Brancher une charge purement resistive connue (chauffage ou lampe
   incandescente, pas une alimentation electronique), puis envoyer
   `AC CAL PHASE_AUTO`. Cette commande corrige le decalage temporel des deux
   voies et leur polarite afin que le PF de cette charge approche 1.
5. Envoyer `AC CAL SHOW` pour verifier les coefficients persistants.

L'acquisition utilise une période fixe de 300 µs, soit environ 3 333 paires
sur une fenêtre complète d'une seconde. Le tampon de 4 096 entrées ne doit
donc plus interrompre prématurément la mesure.

Commandes avancees : `AC CAL PHASE_US <us>`, `AC CAL POLARITY <1|-1>` et
`AC CAL RESET`. Un PF negatif en consommation indique generalement une voie
inversee ; utiliser l'auto-calibration resistive ou verifier le sens IP+/IP-.

Pour une charge non lineaire, le PF affiche est le facteur de puissance reel :
il inclut le dephasage et la deformation des formes d'onde. Il ne doit alors
pas etre interprete comme le seul cosinus d'un angle de phase fondamental.
