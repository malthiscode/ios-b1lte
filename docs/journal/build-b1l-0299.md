# B1L-0299 — C1 : moins de madvise dans le cache de tampons de Mesa (set v142, Mesa)

## Pourquoi

0297 : backboardd fait 16 000 à 23 000 appels `mali 72` (madvise) par 10 s, chacun un aller-retour IOKit (`mach_msg2`),
le premier poste d'appels au pilote. Ils viennent du cache de tampons de panfrost : `panfrost_bo_cache_put` rend le
tampon « évictable », `panfrost_bo_cache_fetch` le reprend. Les pools de lot (64 Ko, `pan_job.c`) entrent et sortent
du cache à chaque image.

## Changement

`userspace/mesa/air-b2l/apply.py` (bloc B1L-0299) sur `pan_bo.c` : les tampons de **moins de 256 Ko** ne passent plus par
`make_evictable` / `make_unevictable`. Ils restent au plus ~2 s dans le cache (`panfrost_bo_cache_evict_stale_bos`,
inchangé) puis sont libérés. Les gros tampons (textures, cibles) restent repris par le noyau sous pression (M1).
Testé à sec sur une copie de `pan_bo.c` (diff attendu). Set v141 → v142.

## À vérifier

`B2L PERF CALLS` : `mali 72` nettement plus bas ; « no memory » pas plus haut que 0297 (26) ; `B2L PROF swaps` ;
ressenti du propriétaire.

## Résultat

Flashé (BOOT `6b97e2d2…`, BOOT-OK). 0298 et 0299 flashés à la suite : le propriétaire teste les deux ensemble.

**madvise (`mali 72`) : 16 000–23 000 → 0 à 3 750 par 10 s.** 0 « no memory » (26 en 0297), pas de plantage, blit 0.
Échantillons noyau `mach_msg2` de backboardd : ~3 000–9 000 par 10 s (~2 000–11 000 en 0297), un peu moins.
i/s : max 21,8 (218 en 10 s), **pas de gain net**. Nouveau premier poste : **création de tampons (`mali 66`) 450–1 430 par
10 s à 1,4–2 ms chacune = 0,9 à 2 s par 10 s**. Puis WAIT_BO (`mali 65`) 5 800–17 800 et `mali 67` 4 600–14 600 par 10 s.
