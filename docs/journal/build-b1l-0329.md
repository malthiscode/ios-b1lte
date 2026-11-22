# B1L-0329 — Mémoire de tuile : pas de réécriture des cibles jetables (Mesa + userspace, set v157)

## Pourquoi

Sur un iPhone, une cible `memoryless` ou stockée `DontCare` reste dans la mémoire de tuile du GPU et n'atteint jamais
la RAM. Ici, chaque passe du verre réécrivait en RAM sa cible RGBA16F plein écran (c1, ~35 Mo) : bande passante et
temps GPU perdus. Le Mali dessine aussi par tuiles, et panfrost sait sauter la réécriture : `invalidate_resource`
sur une cible du lot courant retire son bit de `batch->resolve`, et `pan_job.c` met `rts[i].discard`
(le chemin de `glInvalidateFramebuffer`).

## Changement

- `userspace/mesa/air-b2l/apply.py` (sauvegarde `.before-0329`) : `rwa_texture_discard(texture)` dans
  `air_metal.c` appelle `pipe->invalidate_resource` si la texture est une cible du framebuffer courant.
- `userspace/metal/scion/hakiosmetal_air.m` (sauvegarde `.before-0329`) : à la création d'une passe, les cibles
  memoryless ou `storeAction DontCare` (et `setColorStoreAction:` différé) ; profondeur/stencil seulement si aucun
  aspect n'est gardé. À `endEncoding`, le masque va sur le dernier dessin de la passe, qui appelle
  `rwa_texture_discard` après son `render_draw`. Compteur « not written back » dans `B2L TXL`.
- Mesa reconstruit : cache de shaders invalidé (premier démarrage lent).

## À vérifier

- « not written back » > 0 dans `B2L TXL` ; pas d'image cassée (une cible relue alors qu'elle était jetable) ;
- temps GPU / i/s.

## Résultat

Flashé (BOOT `ace33302…`, BOOT-OK). Pas redémarré : 0330–0331 par-dessus.
