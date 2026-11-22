# B1L-0297 — mémoire d'affichage direct réservée au démarrage (noyau seul)

## Pourquoi

0296 : une image sur deux est copiée par le processeur (~65 ms) parce que la deuxième surface plein écran de backboardd
n'a pas obtenu de mémoire d'un seul bloc (contiguë, sous 4 Gio), que l'écran (DECON) peut lire directement. Ça dépend
du hasard de la mémoire au moment de la demande : obtenue en 0291 (jusqu'à 59,8 i/s), pas en 0269d/0283/0296 (~15 i/s).

## Changement (un seul)

`B2LSurface.cpp` : dès que la forme du panneau est connue (`b2l_surface_set_scanout_shape`, au démarrage de l'écran,
quand l'affichage direct marche), **deux blocs contigus** de `round_page((bpr + 1024) × hauteur)` (~20,6 Mo chacun)
sont réservés sous 4 Gio (mêmes options que l'allocation existante). Les surfaces d'affichage de backboardd les prennent
d'abord (`b2l_scanout_take`), puis l'allocation habituelle si la réserve est vide ; la surface par défaut du noyau
aussi, en secours. Ligne `B2L SURF: B1L-0297: scan-out reserve N: kept|NOT found`. Coût : ~41 Mo réservés.
Noyau seul, set v140.

## À vérifier

`scan-out reserve 0/1: kept`, `scan-out surface for backboardd` présent, `B2L PERF` : blit ≈ 0, `fb 5` bien plus court,
`B2L PROF swaps` : i/s ; ressenti du propriétaire.

## Résultat

Flashé (BOOT `f64469e6…`, BOOT-OK), téléphone redémarré. **Marche** : `scan-out reserve 0/1: kept (20627456 bytes)`,
surface d'affichage de backboardd à 0x8288c000, **blit 0 sur toutes les lignes `B2L PERF`** (plus aucune copie par le
processeur). Propriétaire : un peu plus fluide, comme avant. Mais i/s encore 5 à 22 (max 219 images en 10 s).

Ce qui freine encore (backboardd, par 10 s en régime normal) :
- `mach_msg2` (appels aux pilotes IOKit) : 4 000 à 11 000 échantillons noyau ; ~24 envois GPU (`mali 64`) par image affichée ;
  `mali 72` (madvise) 16 000–23 000 appels, `mali 65` (WAIT_BO) 8 000–11 000, `mali 66` (création de tampon) 600–700 à ~1,3 ms
  chacun (~1 s par 10 s) ;
- `flush` de surfaces jusqu'à 2186 Mo / 462 ms par 10 s par moments ;
- `sysctl` : 32 000 échantillons, mais **seulement au démarrage** (log console) ; 20–180 par 10 s ensuite, pas le frein.
Liquid Glass : passes `glass_foreground_sdf_lph` sur des cibles plein écran 1440×3040 (2 cibles couleur), plus
`glass_background*`, `variable_blur_downsample`. 26 « no memory for a buffer », 0 plantage, 804 attentes WAIT_BO expirées
(0xe00002d6, normal), cache de shaders : 945 trouvés en 171 ms, 5 compilés.
