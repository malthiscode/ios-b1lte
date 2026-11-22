# B1L-0327 — Profil Metal 1 (Apple2) pour le Liquid Glass (userspace, set v155)

## Pourquoi

0326 : le mélange et `edr_scale` du verre sont normaux, mais CoreAnimation envoie une teinte très claire
(`face_cm0` décalage +0,425) et ne lance jamais ses noyaux de mesure de luminosité (`compute_average_luma`,
`calculate_average_luma`, `tile_average_luma`). Le pilote annonce le profil 0, soit la famille Apple1 (A7) seule,
qu'iOS 26 n'a jamais connue. Hypothèse : CoreAnimation y saute l'adaptation du verre au fond.

## Changement

`userspace/metal/scion/hakiosmetal_air.m` (sauvegarde `.before-0327`) :
- `featureProfile` : 1 par défaut (Apple1 + Apple2) au lieu de 0 ; `metal_profile=` le remplace toujours.
  Le commentaire du pilote indique que 0, 1 et 4–9 remplissent la table des limites sans planter.
- `B2L KERNEL` (diagnostic) : chaque noyau de calcul demandé (40 au plus par processus), dans le log du noyau.

## À vérifier

- démarrage et apps sans plantage ;
- `B2L KERNEL` avec un noyau de luminosité ; verre moins blanc sur fond clair.

## Résultat

Flashé (BOOT `6e7945bd…`, BOOT-OK). Log : profil 1 partout ; backboardd demande enfin `compute_average_luma` et `compute_sum_luma`, et ses dispatches tournent (4 à 106 par 10 s). Aucun nouveau plantage (SBRendererService : 1, déjà présent en 0326) ; 0 faute GPU ; 32 « no memory ». Le propriétaire voit toujours des clignotements.

Autres ports : Jesvi-iOS (S20) démarre avec `metal_profile=1` ; ios-pipa et ios-s9plus-exynos répondent Apple1–3 + Common1–2. Ce dernier note que QuartzCore coupe une image en tampons de 128 dessins quand Apple3 est faux (`MetalContext::flush`) et que Core Image veut Apple2.
