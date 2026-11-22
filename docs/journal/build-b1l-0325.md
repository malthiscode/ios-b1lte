# B1L-0325 — Diagnostic du Liquid Glass blanc : ce que le processeur relit des textures (libhakiosmetal, set v153)

## Pourquoi

Le Liquid Glass reste trop blanc, presque opaque, sur les fonds clairs (P23). Le journal ne montre aucun shader
refusé. Le verre d'iOS 26 adapte sa teinte à la luminosité de ce qui est dessous. CoreAnimation calcule cette
luminosité par une chaîne de réductions (`downsample_8_frag_lph` vers 126×50, `variable_blur_downsample` jusqu'à
12×4…).

Hypothèses, dans l'ordre :
1. la luminosité est relue par le processeur (`getBytes`, copie texture → tampon) et revient fausse ou noire ; le
   verre prend alors son aspect « fond sombre » (clair) partout ;
2. la relecture des pixels déjà dessinés dans le shader (`[[color(n)]]`, framebuffer fetch traduit en
   `load_output` / `fb_fetch_output`) donne de mauvaises valeurs pour une cible BGRA8 ou RGBA16F ;
3. l'espace de couleur : une correction gamma appliquée deux fois.

## Changement

`hakiosmetal_air.m` : une ligne `B2L READ` toutes les 10 s (avec `B2L TXL`), qui donne :
- le nombre de `getBytes`, avec taille, format et 16 premiers octets du dernier ;
- le nombre de copies texture → tampon, avec taille et format de la dernière.

Ce build ne corrige rien ; il départage les hypothèses.

## À vérifier

- `B2L READ` présent pendant que le verre est à l'écran ? Valeurs à zéro (noir) ?
- Absent : la luminosité reste sur le GPU, donc hypothèse 2 ou 3.

## Résultat

Flashé (BOOT `45781702…`, BOOT-OK), téléphone redémarré. Pas encore testé.
