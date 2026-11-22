# B1L-0328 — Profil Metal 4 (Apple1–3) et relevé de la luminosité (userspace, set v156)

## Pourquoi

0327 : avec le profil 1, backboardd lance enfin `compute_average_luma` / `compute_sum_luma`, mais le verre reste
blanc (propriétaire). ios-s9plus-exynos (S9+ Exynos, Mali) répond Apple1–3 + Common1–2 : sans Apple3, QuartzCore coupe
une image en tampons de 128 dessins (`MetalContext::flush`). Le propriétaire demande de se présenter en Apple3.

## Changement

`userspace/metal/scion/hakiosmetal_air.m` (sauvegarde `.before-0328`) :
- `featureProfile` : 4 par défaut. D'après les notes de hakios-xnu-kernel (`air_to_nir.c`), Apple4 (tile shaders,
  que Mesa ne sait pas faire) n'apparaît qu'au profil 5 ; le commentaire du pilote dit que 4 remplit la table des limites.
- `B2L FAMILY` : les familles Apple1–9 réellement annoncées (`supportsFamily:`), une fois par processus.
- `B2L LUMA` : la valeur float4 écrite par `compute_sum_luma` (lue au dispatch suivant), 30 au plus.

## À vérifier

- `B2L FAMILY` : Apple1–3 = 1, Apple4+ = 0 ; démarrage et apps sans plantage ;
- `B2L LUMA` : une luminosité plausible (0–1) qui change avec le fond ; sinon la réduction est fausse sur Mali.

## Résultat

Flashé (BOOT `fcc7aa97…`, BOOT-OK). Testé avec 0329–0331 : en Apple3, CoreAnimation passe l’écran en BGRA10_XR (550), inconnu de Mesa → backboardd plante, rendu CPU. Retiré en 0332.
