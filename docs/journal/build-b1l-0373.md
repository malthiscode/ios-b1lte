# B1L-0373 — luminosité : tout le curseur (set v168)

**Demande.** « Ca baisse et ca augmente juste je peut le baisser et l'augmenter que sur une partie du curseur » (0372).

## Changement (`B2LPanel.h`, sauvegarde `.before-0373`)
- La correspondance nits → AOR couvre tout ce qu'iOS envoie (BrightnessMilliNits 1..1060 nits), sur une échelle logarithmique :
  - 1 nit → AOR 0x0BC9 : niveau le plus sombre de la table normale de Samsung, c'est le plancher, jamais noir ;
  - 1060 nits → AOR 0x0014 : niveau le plus haut de la table normale, sans HBM ;
  - 64 points (nits_i = 1060^(i/63), lignes émises = 47 × (3044/47)^(i/63)), avec interpolation linéaire entre les points.
- L'écran peut donc devenir plus clair qu'au démarrage (AOR 0x0440 → 0x0014 : environ 1,5 fois plus de temps d'émission). Le gamma de S-Boot reste inchangé.
- Le journal ne note plus que la fin de chaque fondu (200 lignes au plus).

## Vérifié sur le Mac
`tests/test-panel-dcs.cpp` :
- de 0 à 2200 nits : la valeur reste dans 0x0014..0x0BC9 et la courbe est monotone ;
- bornes exactes : 1 nit → plancher, 1060 et 2175 nits → 0x0014 ;
- le point milieu est bien logarithmique.

## Non vérifié, risques
- La luminosité maximale réelle dépend du gamma laissé par S-Boot. Elle n'est pas mesurée.
- La persistance après un redémarrage n'est pas encore confirmée. iOS a envoyé 155 nits au démarrage en 0370 et en 0372 : c'est peut-être sa valeur par défaut, pas la valeur gardée.

## Flash
Noyau seul, set v168 inchangé. SYSTEM-OK, BOOT `63ffe476…` relu (BOOT-OK). Commande de redémarrage envoyée.

**Résultat.** Non testé.
