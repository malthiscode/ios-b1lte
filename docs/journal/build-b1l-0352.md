# B1L-0352 — jeu, édition et galerie de Photos (set v167, GL des apps v14, noyau)

**Demande.** « build tout en un coup » : les trois corrections proposées après le test 0351, dans un seul build.

## 1. Jeu : lecture de diagnostic retirée
`userspace/b2l_eagl_app.m` relisait un pixel de la surface (présentation 1, puis toutes les 600). Ce mappage faisait nettoyer la surface avant chaque tâche (règle de 0351). La lecture et le champ « middle pixel » du journal sont retirés.

## 2. Plantage à l'édition : traducteur AIR
Le shader de détection de visages (`cnnConvArray_*`, MPS) demandait trois choses absentes, chacune cachée derrière la précédente :
- `air.is_uniform` (vote NIR sur le groupe, 8 voies sur G76) ;
- `llvm.assume` (promesse à l'optimiseur, rien à émettre) ;
- un bitcast `<2 x i32>` vers `<4 x half>` (`nir_extract_bits`).

Source : hakios-metal, MIT (`window-server/tools/mesa/air/air_intrinsics.c:1314-1326` et `:1640-1643`, `air_to_nir.c:2944-2948`). Deux écarts : vecteurs flottants reconnus (`v2f16`), et booléen converti (`is_uniform.i1`).

## 3. Galerie
- 28 formats ASTC LDR et sRGB, échantillonnés seulement : envoi, lecture et copie par rangées de blocs, régions vérifiées. Le noyau transmet à Mesa le registre TEXTURE_FEATURES0 du G76.
- Tampon constant déclaré mais non lié : il lit des zéros (même règle que les textures non liées de B1L-0228), au lieu d'abandonner le dessin. Dans Tungsten (`fragment_main`), `shaderP` est au tampon 5 sans condition de constante de fonction, et Photos ne le lie pas toujours.
- Le diagnostic `B2L PUT` ne lit qu'une rangée pour un format compressé (sinon lecture hors de la mémoire de l'app).

## Vérifié sur le Mac
- `tests/test-air-0352.py` : régions, rangées, boucle de lecture, tampon de zéros, 28 formats (valeurs comparées à l'en-tête du SDK).
- 4 variantes de `cnnConvArray` spécialisées par le MPS de ce Mac (macOS 26, GPU Intel) : traduites et compilées jusqu'au code Bifrost G76, assertions actives. Ce ne sont pas les modules exacts du téléphone (`3xIn_16xOut_1_2`). Le module générique, non spécialisé, reste refusé (pointeur chargé de la mémoire).
- `uniform_id.ll` de hakios-metal : 2 votes émis, compilé pour G76. Dans les 4 variantes MPS, `is_uniform` ne nourrit que des `assume` et disparaît.
- `test-eagl-present.py`, `test-surface-flush.py` : ok.

## Non vérifié
Rendu ASTC sur le GPU (softpipe ne le fait pas), résultat des calculs MPS, galerie réelle. Le tampon de zéros vaut pour toutes les apps : des dessins jusque-là abandonnés vont apparaître.

## Flash
GL v14 `4d9ab2bc…`, libhakiosair `6ec742e2…`, libhakiosmetal `481feb1b…`, SYSTEM-OK deux fois, BOOT `c77b5271…` (BOOT-OK, relu). Sauvegardes `.before-0352`.

## À lire au prochain journal
- jeu : plus de `surface N mapped as a buffer` pour ses surfaces, nettoyages par image (2,35 en 0351), envoi (4,3-4,9 ms), i/s produites et affichées (57 et 38,5) ;
- Photos : plus de `format 204 is one of the ASTC`, plus de `reads buffer 5`, lignes `B1L-0352 … is not bound`, plus d'abandon `unable to load function` ;
- autres refus vus en 0351, non traités : `air.is_null_texture_2d` (MediaRemoteUI), « argument 3 is not a constant », « a struct where a number belongs », import 420f à deux plans.

**Résultat (testé le 2026-11-22, `out/logs/0352-boot.txt`).** Vu par le propriétaire : galerie inchangée (photo visible seulement en la touchant) ; jeu bien plus fluide, sauf dans les niveaux complexes.

Jeu : plus aucune surface mappée comme tampon. Fenêtres de 10 s (i/s produites interpolées, dessins et durées médians des images journalisées, soit 1 sur 20 environ) :

| Fin (ligne) | Produites | Affichées | Dessins par image | Travail GL par image |
|---|---|---|---|---|
| 15026 | 60,0 | 57,8 | 17 | 11,2 ms |
| 16030 | 58,2 | 56,7 | 49 | 11,5 ms |
| 16677 | 31,9 | 28,9 | 57 | 17,4 ms |
| 17394 | (non mesuré) | 29,1 | 74 | 23,8 ms |

Dans les scènes chargées, le jeu dépasse lui-même les 16,7 ms d'une image et tombe à 30. La durée va jusqu'à glFinish : part processeur et part GPU non séparées.

Photos : plus de refus ASTC, plus de dessin abandonné, pas d'abandon journalisé (le propriétaire n'a pas dit avoir réessayé l'édition). Galerie : la cause donnée en 0351 et visée ici était fausse ou insuffisante. La vraie est trouvée en 0353 (`build-b1l-0353.md`). Imports 420f à deux plans toujours refusés, Photos charge alors une texture BGRA.
