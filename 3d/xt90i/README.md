# Boîtiers XT90i imprimables

## Fichiers

- `xt90i_male_boitier.stl` — boîtier avec cavité de réception, orienté pour l’impression.
- `xt90i_femelle_boitier.stl` — boîtier avec fiche extérieure, orienté pour l’impression.
- `xt90i_test_cavites_male.stl` — coupon avec cinq jeux de cavité, de 0 à 0,20 mm par côté.
- `xt90i_test_fiches_femelle.stl` — coupon avec cinq jeux de fiche, de 0 à 0,20 mm par côté.
- `xt90i_gen.py` — source paramétrique pour modifier les dimensions et régénérer les STL.

## Impression

Les fichiers STL sont en millimètres et déjà orientés face plate sur le plateau. Les dimensions hors tout du maillage sont :

| Pièce | Encombrement X × Y × Z |
|---|---:|
| Boîtier mâle | 11,4 × 30,7 × 22,56 mm |
| Boîtier femelle | 11,4 × 29,7 × 22,56 mm |

Imprimer d’abord les deux coupons dans le même matériau et avec le même profil que les boîtiers. Tester les fiches femelles dans les cavités mâles avant d’imprimer les pièces finales. Les compensations par défaut sont de 0,10 mm par côté sur chaque pièce ; le générateur calcule un jeu minimal de profil de 0,35 mm entre les deux pièces imprimées. Ce résultat géométrique ne prédit pas exactement le comportement de l’imprimante : utiliser les coupons pour choisir le jeu adapté.

## Régénération

Depuis la racine du dépôt, Python 3 avec `numpy` et `scipy` est nécessaire :

```powershell
python -m pip install numpy scipy
python 3d/xt90i/xt90i_gen.py --outdir 3d/xt90i
```

Pour modifier le jeu d’emboîtement :

```powershell
python 3d/xt90i/xt90i_gen.py --cav-comp 0.10 --plug-comp 0.10 --outdir 3d/xt90i
```

## Limites des références

Les cotes marquées `[PLAN]` dans le générateur viennent du plan fourni. La largeur extérieure, la profondeur de cavité, certains rayons et chanfreins sont estimés à partir des vues. Les diamètres des passages de contacts dépendent des contacts métalliques réellement utilisés ; les mesurer avant de modifier `M_POWER_D`, `F_POWER_D` ou `SIG_D`.

Ces STL représentent les boîtiers en plastique. Ils ne contiennent pas les contacts conducteurs et ne constituent pas à eux seuls un connecteur électrique prêt à être câblé. L’ajustement mécanique et l’assemblage avec les contacts réels restent à vérifier sur le coupon et sur les pièces imprimées.
