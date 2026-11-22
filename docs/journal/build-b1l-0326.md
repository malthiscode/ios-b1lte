# B1L-0326 — Diagnostic du Liquid Glass blanc (userspace, set v154)

## Pourquoi

Le propriétaire (0325) : sur un même élément de verre à cheval sur du clair et du sombre, la partie sombre est juste et
la partie claire devient toute blanche. Les éléments de verre et les sliders/toggles de Réglages clignotent.

Ce que 0325 et la lecture des shaders ont déjà écarté :
- pas de relecture CPU de l'image (aucune ligne `B2L READ`) ;
- pas de mélange dual-source (`[[color(0), index(1)]]`) : aucun refus dans le log ;
- les shaders `glass_background_lph`, `glass_foreground_sdf_lph` et `variable_blur_downsample_frag_lph` sont traduits
  sans défaut visible (la somme des poids du flou fait 1 ; la conversion float → bool est `x != 0`).
- le verre applique au fond flouté trois matrices de couleur 3×4 (uniformes 16–24), puis un mélange piloté par la
  luminance (0,2126/0,7152/0,0722). Le résultat dépend donc des valeurs envoyées par CoreAnimation.

## Changement

`userspace/metal/scion/hakiosmetal_air.m` (sauvegarde `.before-0326`), lignes envoyées au noyau (`kern.hakios.say`,
sans la limite de 3000 lignes) :
- `B2L GLASSPIPE` : pour chaque pipeline dont le shader de fragment contient « glass » (24 au plus), l'état de mélange
  des attachements 0–2 et le label CoreAnimation ;
- `B2L GLASSBUF` : pour les 8 premiers dessins `glass_background*`, le contenu (240 octets au plus) de chaque tampon
  de fragment lié.

## À vérifier

- mélange des pipelines de verre (source-over prémultiplié attendu : `1/5`) ;
- valeurs des matrices de couleur et du mélange par luminance, à rejouer sur le Mac.

## Résultat

Flashé (BOOT `763f7e55…`, BOOT-OK). Propriétaire : aucune amélioration (attendu, diagnostic seul). Log :
- mélange des pipelines de verre normal : source-over prémultiplié (`1/5`), attachement 1 masqué ;
- `edr_scale` = 1,0 ;
- `face_cm0` = 0,91 / −0,30 / −0,03, décalage +0,425 : CoreAnimation envoie une teinte très claire (lignes coupées vers 95 octets par le noyau) ;
- aucun pipeline de calcul dans backboardd : `compute_average_luma` / `calculate_average_luma` (mesure de luminosité du fond) ne tournent jamais. Tous les processus annoncent le profil 0 (Apple1 seule).
- « no memory » : 4.
