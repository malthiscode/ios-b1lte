# B1L-0306 — Les tampons gardent leur vue processeur dans le cache de Mesa (Mesa, set v146)

## Pourquoi

0304/0305 : après les réveils de fils, le premier coût noyau de backboardd est `pmap_remove` sur des pages câblées :
Mesa démappe chaque tampon qui entre dans son cache (`panfrost_bo_unreference` → `panfrost_bo_munmap` →
`IOConnectUnmapMemory64`), puis le remappe à la réutilisation (`IOConnectMapMemory64`, avec `kIOMapPrefault` depuis
0293 : toutes les pages entrées de nouveau).

## Changement

`apply.py`, bloc B1L-0306 sur `pan_bo.c` : plus de `panfrost_bo_munmap` dans `panfrost_bo_unreference` ; c'est
`panfrost_bo_free` qui démappe, quand le tampon quitte vraiment le cache (2 s, plafond 160 Mo, purge) ou n'y entre pas.
Testé à sec sur une copie de `pan_bo.c`. Set v145 → v146.

## À vérifier

KSTK : `pmap_remove` / `vm_map_delete` nettement plus bas ; `B2L COMMIT` (Mesa draw) plus bas ; i/s ; mémoire stable ;
0 plantage.

## Résultat

Flashé (BOOT `7f7d60eb…`, BOOT-OK). **Mieux** : i/s par 10 s en régime normal 14 à 34 (moyenne ~19,5) contre 2 à 24
(moyenne ~12) en 0305 ; max 33,7. KSTK : `pmap_remove` 13 000 → ~1 000, `vm_map_delete` 13 000 → 1 300, `madvise`,
défauts de page et `pmap_enter` ~0 dans le top. 0 « no memory », 0 plantage de backboardd (analyticsd plante, sans
rapport). Restent : réveils de fils (`Call_continuation`) 33 000, **attente disque UFS 18 000** (régime normal aussi),
attentes GPU (`hakios_mali_wait`) ~4 800, `hakios_mali_unmap` (vidage de cache par entrée) ~3 200.
`B2L COMMIT` : Mesa draw encore 1,1 à 3,4 s par 10 s (~0,1–0,25 ms par dessin).
