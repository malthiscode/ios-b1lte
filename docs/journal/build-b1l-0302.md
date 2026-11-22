# B1L-0302 — Purge des tampons GPU selon la mémoire disponible, pas la mémoire libre (noyau, set v143)

## Pourquoi

0300/0301 : chaque création de tampon lance `b2l_mali_purge` parce que la mémoire **libre** (`vm_page_free_count`) reste à
30–44 Mo, sous le seuil de 256 Mo (B1L-0258). Or iOS remplit volontairement la RAM de cache récupérable : la mémoire libre
est toujours basse. Résultat : les gros tampons rangés dans le cache de Mesa sont repris, Mesa les recrée — 290 à
1 330 Mo de tampons ≥ 4 Mo créés par 10 s, 0,9 à 2,4 s de création par 10 s dans backboardd.

## Changement

`B2LMaliService.cpp`, `createBO` : seuil sur `memorystatus_get_available_page_count()` (mémoire disponible comptée par
memorystatus = `AVAILABLE_NON_COMPRESSED_MEMORY`, `osfmk/vm/vm_pageout.c`, déclarée dans `bsd/sys/kern_memorystatus_xnu.h`)
au lieu de `vm_page_free_count`. La purge après un échec d'allocation (second essai) est inchangée. La ligne
`B1L-0258: … taken back` donne aussi la mémoire disponible. Noyau seul.

## À vérifier

`B2L CREATE` : purge ~0 ms, beaucoup moins de tampons ≥ 4 Mo ; `B2L RTT` 0x42 bien plus bas ; « no memory » = 0 ;
pas de plantage par manque de mémoire ; i/s.

## Résultat

Flashé (BOOT `813b3c2e…`, BOOT-OK). **Propriétaire : aucune amélioration.** Log : purge 0 ms partout (corrigé),
créations (0x42) 0,1 à 0,67 s par 10 s (0,9–2,4 s avant), 0 « no memory », mais i/s max 25,5 : la création n'était pas
ce qui limite l'image. Nouveau signe : `B2L HOLD` — chaque tampon de commandes de backboardd garde le verrou Mesa
**30 à 35 ms en moyenne** (1 à 2,9 s par 5 s, jusqu'à 284 ms) ; `newTextureWithDescriptor` 0,4–1 s par 5 s. → B1L-0303.
