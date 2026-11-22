# B1L-0296 — temps par appel de pilote écrit par défaut (noyau, mesure seule)

## Pourquoi

0295 : l'appel système qui occupe le plus le noyau dans backboardd est `mach_msg2_trap`, le chemin de tous les appels
aux pilotes IOKit (GPU, surfaces, écran). Le compteur par appel (`B2L PERF CALLS`, B1L-0102) existe déjà.

## Changement (un seul)

`B2LFramebuffer.cpp` : le rapport `B2L PERF` / `B2L PERF CALLS` (toutes les 10 s) est écrit par défaut ; `b2l_perf=0`
le coupe. Les boot-args sont dans le chargeur UEFI, non touché. Noyau seul, set v140.

## Résultat

`B2L PERF CALLS` : **`fb 5` (SwapEnd) 2 à 5 s par 10 s** ; `B2L PERF` : blit 21 à 58 fois, 2 à 5 s, 350 à 970 Mo par 10 s
(~65 ms par copie de 17 Mo). Les images alternent entre la surface 2 (`fClientSurface`, contiguë : affichage direct)
et la surface 3 de backboardd (pas de ligne `scan-out surface for backboardd` : mémoire ordinaire), **copiée par le
processeur une image sur deux** → ~15 i/s au plus. Comparaison : la surface contiguë a été obtenue en 0291 (59,8 i/s
au mieux), pas en 0269d, 0283 ni 0296 : 18 Mo d'un seul bloc sous 4 Gio, demandés une minute après le démarrage,
selon l'état de la mémoire. Les autres appels de pilote sont petits (mali 66, 9, 125, 64 : 0,1 à 0,5 s par 10 s).
