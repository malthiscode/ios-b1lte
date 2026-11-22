# B1L-0304 — Mesure : pile d'appels du noyau à chaque échantillon (noyau, set v144)

## Pourquoi

0303 : le temps des dessins de backboardd est surtout du noyau, mais les échantillons tombent au moment où les
interruptions sont réactivées (`ml_set_interrupts_enabled`), ce qui cache la fonction qui a pris le temps.

## Changement

`osfmk/arm64/b2l_prof.c` : à chaque échantillon noyau, 4 adresses de retour en remontant les cadres (pointeur de cadre),
seulement à l'intérieur de la pile noyau du fil (bornes vérifiées, cadres qui montent seulement). Regroupées ; les 20
chaînes les plus vues toutes les 10 s : `B2L PROF KSTK: N kpc … < f0 < f1 < f2 < f3 x16 …`. Mesure seule, noyau seul.

## Résultat

Flashé (BOOT `be9bcbf8…`, BOOT-OK). Suivi tout de suite de 0305 : testés ensemble.

**Piles noyau de backboardd (log 0305), en régime normal, par 10 s** : ~2 000–7 000 échantillons noyau, dont
- reprise de fil (`Call_continuation`, réveils de fils GCD / workq) 300–2 400 ;
- **`pmap_remove` via `vm_map_delete` sur des pages câblées (`pte_set_wired`) 500–3 200** : démappage de mémoire IOKit,
  c.-à-d. les tampons GPU que Mesa démappe à chaque passage par son cache (`IOConnectUnmapMemory64`) → B1L-0306 ;
- `madvise` de libmalloc (pages réutilisables, `bzero_phys`) ~4 000 sur la durée, défauts de page ~4 600.
- **Lecture disque : `b2l_ufs_transport_read` → `command` → `delay(1)` en boucle** (attente du contrôleur UFS par pas
  de 1 µs, avec endormissement et réveil à chaque pas) : 38 500 échantillons sur la durée, par pics (jusqu'à 16 500 par
  10 s) quand backboardd lit des fichiers. Coût système général à chaque lecture de fichier, hors pilote GPU.
Vérifié aussi : Mesa démappe bien par `hakios_drm_unmap` (IOConnectUnmapMemory64), pas de fuite de vues IOKit.
