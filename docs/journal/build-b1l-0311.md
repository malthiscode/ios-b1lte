# B1L-0311 — Textures memoryless recyclées (libhakiosmetal, set v149)

## Pourquoi

0310 : la moitié des textures de backboardd (776–1 557 par 10 s) sont `MTLStorageModeMemoryless`, et la plupart vivent
moins de 500 ms. Faire et défaire les textures coûte 0,8 à 1,9 s par 10 s, sous le verrou Mesa.

## Changement

`hakiosmetal_air.m` :
- Une texture memoryless libérée est gardée, sans surface ni tampon dessous. Le pool fait au plus 32 textures et
  96 Mo (compté à 8 octets par pixel), et une texture n'y reste pas plus de 2 s.
- La prochaine demande de même forme la reçoit telle quelle : même format, taille, type, niveaux, échantillons et usage.
- Pas de remise à zéro. Metal ne donne aucun contenu à une texture memoryless : ses passes la vident ou l'ignorent.
- Les vues gardent leur parent (B1L-0105), donc une texture n'est rendue au pool qu'une fois sa dernière vue partie.
- `B2L TXL` dit `memoryless N (reused R)`.

## À vérifier

- `reused` proche de `memoryless`.
- Coût de création en baisse.
- Aucune image avec des restes d'une autre.
- i/s ; touches de la Calculatrice.

## Résultat

Flashé (BOOT `dfeaf127…`). Le propriétaire : « un peu mieux », mais il reste de la latence au toucher et le Liquid
Glass reste lent.

- `B2L TXL` : memoryless 456–1 408 par 10 s, dont **247–841 réutilisées** (30–60 %).
- i/s par 10 s : 12 à 40.
- **GPU occupé seulement 28–41 %**.
- Chez backboardd, par 10 s :
  - `fb 5` (SwapEnd) : 290 × 2 017 ms, ~7 ms chacun. swap() attend que le GPU ait fini la surface (HakiosMaliDrawn).
  - submit 13 600 ; WaitBO 15 000 (0,43 s) ; CreateBO 400 (0,5 s) ; GemClose 320 (0,25 s).
  - Mesa draw 2,9 s pour 18 800 dessins.
- Fonction utilisateur la plus chaude : encore le memcpy de `panfrost_emit_const_buf`. Ce sont les tampons de
  constantes à décalage non aligné, toujours copiés, et lus depuis une mémoire write-combine.
- Appels système : `workq_kernreturn` 2 878 échantillons, `madvise` 841.
