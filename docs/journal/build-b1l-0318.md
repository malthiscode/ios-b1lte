# B1L-0318 — Rendre la réserve de textures quand le noyau refuse la mémoire (libhakiosmetal, set v151)

## Pourquoi

0317 : il reste 464 refus pour des cibles de rendu de 16 à 35 Mo, avec 1,5 Go de tampons GPU vivants. Mesa vide déjà
son cache de tampons et réessaie. La réserve de textures de 0311/0315 (jusqu'à 128 Mo, des cibles de 35 Mo) n'était
rendue qu'après 1 s, et seulement si une autre création de même forme passait.

## Changement

`hakiosmetal_air.m`, `newTextureWithDescriptor:` : si `texture_create` échoue et que la réserve n'est pas vide, on
détruit toute la réserve et on réessaie une fois. Une ligne `B2L TXL: creation refused, pool of N textures (M MB) given
back` est écrite (20 au plus). Le noyau garde le correctif 0317. Mesa n'est pas reconstruit.

## À vérifier

- Les lignes `creation refused` : à chacune, le « no memory » qui suit doit avoir été rattrapé (moins de refus
  qu'en 0317, et moins de blocs noirs).
- S'il reste des refus avec une réserve vide, il faudra regarder la plage d'adresses partagée (2 880 Mo), ou baisser
  la mémoire gardée.

## Résultat

Flashé (BOOT `37675c4a…`, BOOT-OK), téléphone redémarré. Session courte (~2 min d'usage).

- « no memory » : **464 → 3**, tous de 19,6 Mo. Aucune ligne `creation refused` : ces 3 tampons ne sont pas des
  textures (tampons internes de Mesa). Pas de faute GPU.
- 1,2 Go de tampons GPU vivants au pire ; backboardd 758 Mo, dont 102 Mo purgeables.
- i/s par 10 s : 17 à 29.
- Coût restant (backboardd, par 10 s) : `CreateBO` (0x42) 1 205 appels, **1,6 s** ; `GemClose` (0x9) 1 268 appels,
  **0,9 s** ; textures ≥ 1 M px : 292 créées, 1,8 s. Environ un quart du temps de backboardd part à créer et
  détruire de la mémoire GPU. C'est la prochaine piste.
