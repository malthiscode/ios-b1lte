# B1L-0353 — miniatures de Photos (set v168, dernier build demandé)

**Demande.** « fait un dernir build et apres on s'arrete » : galerie sans miniatures, niveaux complexes lents.

## Cause des miniatures invisibles
- Au lancement de Photos, avant tout toucher, la grille dessine trois textures (ASTC 4092x992, BGRA 360x760, RGBA 480x1013) avec `vertex_main` + `fragment_main` de Tungsten. Aucune ne se voit : la panne est commune au shader.
- `fragment_main` reçoit l'opacité en varying **plat de 16 bits** (`half alpha [[flat]]`). Bifrost avant v9 ne charge un varying plat qu'en 32 bits (`assert(sz == 32)`, `bi_emit_load_vary`), et le Mesa du téléphone est construit sans assertions.
- Le sommet écrit en 32 bits depuis B1L-0090. Déduit de ce code, non mesuré sur le GPU : le fragment lit les 16 bits bas, soit 0 pour 1,0, donc une vignette transparente.
- Reproduit sur le Mac : `fragment_main` spécialisé par le Metal du Mac arrête le compilateur G76 sur cette assertion.

## Correctif
`userspace/mesa/air-b2l/apply.py`, bloc B1L-0353 : miroir de B1L-0090 côté fragment. Un varying plat de 16 bits est chargé en 32 bits puis réduit selon son type (flottant, signé, non signé).

## Vérifié sur le Mac
- `tests/test-air-0353.py` (half, short, ushort plats) : passe, et échoue sans le correctif sur la même assertion.
- Tungsten : 18 combinaisons de constantes compilées (coins arrondis, découpe, effet). Ce ne sont pas forcément les variantes du téléphone.
- 80 modules sauvegardés compilent. Les 22 autres échecs sont tous des refus de traduction, avant le compilateur.
- `test-air-0352.py`, `test-eagl-present.py`, `test-surface-flush.py` : passent.
- Dans la bibliothèque livrée, `bifrost_preprocess_nir` passe de 888 à 1011 instructions, avec la réécriture attendue.

## Non vérifié, risques
- Rien n'est testé sur le téléphone. Rendu ASTC jamais vu.
- Le correctif vaut pour toutes les apps : tout shader à varying plat de 16 bits change de rendu.
- La clé du cache de shaders est l'UUID de la bibliothèque : le premier démarrage recompile tout et sera lent.

## Niveaux complexes : pas de changement
Les mesures de 0352 (`build-b1l-0352.md`) mettent la limite dans le travail GL du jeu (17 à 24 ms par image pour 57 à 74 dessins). Il faut d'abord séparer processeur et GPU dans ces images (piste 6 de `analyse-framerate-0349.md`).

## Flash
libhakiosair `c021dd42…`, libhakiosmetal `93eb6f0f…`, SYSTEM-OK, BOOT `1262685d…` (BOOT-OK, relu), commande de redémarrage envoyée. GL des apps v14 inchangée. Sauvegardes `.before-0353`.

**Résultat.** Non testé. Travail arrêté ici à la demande du propriétaire.
