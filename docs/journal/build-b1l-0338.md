# B1L-0338 — Fonds d'écran d'Apple : échantillonnage libre et surfaces 'L008' (Mesa + libhakiosmetal, set v164)

## Pourquoi

0337 : PosterBoard, MercuryPosterExtension et CollectionsPoster ont le Mali, mais tous les fonds restent noirs.
- PosterBoard : `the draw failed: downsample_4_frag_lph samples texture 3 and that texture was made without MTLTextureUsageShaderRead`
  (6 fois). Sur un GPU Apple, l'usage n'est qu'une indication : la texture s'échantillonne quand même.
- CollectionsPoster : `no drawable: surface N is 'L008' and Metal format 10 is 'A008'`, `B2L IMPORT: 32 asked, 31 refused`.
  'L008' (luminance, 1 octet) a la même disposition que 'A008'.

## Changement

- `userspace/mesa/air-b2l/apply.py` (sauvegarde `.before-0338`) : `air_metal.c` met `read = true` avant de calculer les
  `bind` d'une texture ; toutes les textures ont `PIPE_BIND_SAMPLER_VIEW` et `shader_read`.
- `userspace/metal/scion/hakiosdrawable.m` (sauvegarde `.before-0338`) : un format d'un octet (R8Unorm) est accepté sur une
  surface 'L008'.
- Set v164, `--mesa`.

## À vérifier

- vignettes et fonds d'Apple visibles ; plus de « made without MTLTextureUsageShaderRead » ni de refus 'L008' ;
- le verre sur un vrai fond d'Apple.

## Résultat

BOOT-OK (boot c3edf329…), changement Mesa vérifié dans `air_metal.c`, téléphone redémarré. **Propriétaire : les fonds d'écran ne marchent toujours pas.** Log : les deux refus de 0337 ont disparu ; nouveau blocage, `VTMTSComputeFunction1x1` de CollectionsPoster refusé 72 fois (mémoire de groupe de threads sans taille donnée par `-setThreadgroupMemoryLength:atIndex:`).
