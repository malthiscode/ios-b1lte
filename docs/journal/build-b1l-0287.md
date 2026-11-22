# B1L-0287 — retour au pilote Metal de 0269 (set v133)

Demande du propriétaire (2026-11-21) : « revenir à celui qui marchait et reconstruire dessus ». Il précise qu'avant 0285,
tout marchait même avec fairplayd (0284) : la cause des plantages est donc mon G1 (0285/0286), pas fairplayd.

## Changement (un seul)

- Noyau : `MetalPluginName` et le dossier des bibliothèques reviennent de `B2LMetalv135` / `b2l-metal-v135` à
  `B2LMetalv133` / `b2l-metal-v133` (le set de 0269, toujours présent sur SYSTEM). Fichiers : `B2LMetalAccel.cpp`,
  `b2l_userspace_seed.h`, `b2l_exec_graphics_diag.h` (xnu-overlay puis xnu/).
- Build `--no-libs` : SYSTEM n'est pas réécrit pour Metal. Les changements noyau des autres sessions (0270–0284) restent.
- Arbre Mesa : `pan_bo.c` est remis à son état d'avant 0285, pour que le prochain build `--mesa` parte de la bonne base.

## À vérifier après le démarrage

- ligne `MetalPluginName B2LMetalv133` dans le log ;
- plus de `no memory for a buffer` ni de `code=0xa` ; mémoire libre comme en 0283.

## Résultat

Flashé (BOOT `d5d14448…`, BOOT-OK), téléphone redémarré vers iOS. À tester par le propriétaire.
