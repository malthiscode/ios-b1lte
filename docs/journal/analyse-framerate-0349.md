# Analyse de la fréquence d'image après B1L-0349

Source : `out/logs/0349-boot.txt` (epoch 358). Analyse seule, aucun code modifié.

## En bref
- Le GPU n'est pas la limite : 15 % occupé en jeu, 43 % au plus dans l'interface.
- Le temps part dans le processeur : copie de présentation du jeu, entretien de cache et réimport des IOSurfaces, attentes en série dans le fil de rendu.

## Geometry Dash : 29,6 i/s en médiane, plateau à 30,0, creux à 16,6
- Mesuré : travail GL de l'app 5 à 14 ms par image, 17 à 33 ms dans les scènes lourdes (`hakios-gl: frame … us`). Cette durée s'arrête à glFinish, avant la copie.
- Lu dans `userspace/b2l_eagl_app.m` `my_presentRenderbuffer` : ensuite `glReadPixels` de 3040x1440 (17,5 Mo), puis retournement et conversion RGBA→BGRA pixel par pixel.
- Déduit, non chronométré : cette fin d'image coûte environ 11 à 17 ms. À 5,2 ms de GL le jeu est déjà à 30, à 17 ms il tombe à 24, au-delà de 21 ms vers 17-19. Cela suit le passage à 2 puis 3 périodes de 16,7 ms. Une limite volontaire du jeu à 30 n'expliquerait pas ces paliers, mais n'est pas exclue pour le plateau.

Présentation par backboardd, par image du jeu (fenêtre lignes 35065-35600, 296 images en 10 s). Les compteurs `PERF CALLS` couvrent tous les processus : l'attribution à backboardd est déduite du nombre d'appels, et l'attente GPU peut contenir celle du glFinish du jeu.

| Poste | ms | Source |
|---|---|---|
| Nettoyage de cache (2 × 16,7 Mo) | 3,4 | `flush 592 x, 995 ms` |
| Démontage GPU de l'IOSurface | 2,8 | `mali 9` (GEM_CLOSE) |
| Réimport GPU de l'IOSurface | 1,9 | `mali 126` |
| Attente GPU (backboardd + jeu, non séparés) | jusqu'à 2,9 | `mali 125` |
| SwapEnd | 2,7 | `fb 5` |
| Total | 10,8 à 13,7 sur 16,7 | |

## Interface (les moyennes par 10 s comptent aussi l'écran immobile)
- Fenêtre rapide (49300-49900, 427 images en 10 s, 42,7 i/s). Par image : commit 3,8 ms, SwapEnd 7,4, attente GPU 4,1, création de tampons 1,6, soit 16,9 ms : toute la période. Les trois derniers postes viennent de compteurs communs à tous les processus.
- Fenêtre lourde (56500-56800, 195 images en 10 s, 19,5 i/s). Par image : commit 31,5 ms, et 11,8 ms de nettoyage de cache (2,3 s et 17,6 Go en 10 s, environ 31 nettoyages par image). `flushBeforeJob` nettoie toute la surface à chaque tâche, même si le processeur n'y a rien écrit.
- Fil de rendu dans cette fenêtre : 62 % sur A75, 27 % sur M4, 11 % sur A55. À 83-85 °C le M4 descend à 936 MHz.

## Pistes classées (gains estimés, non testés)
1. **Présentation GPU du jeu** : dessiner dans l'IOSurface ou y faire une copie GPU retournée (`hakios_gl_surface_texture` existe). Enlève 11-17 ms. 60 probable dans les menus, juste dans les scènes à 14 ms, 30-40 dans les scènes lourdes. Risque moyen : orientation, couleurs, synchronisation.
2. **Garder le tampon GPU de chaque IOSurface** au lieu de démonter et remonter 4275 pages par image : environ 4,7 ms par image en jeu. Risque moyen : durée de vie des surfaces.
3. **Nettoyer le cache seulement après une écriture processeur** (seed d'IOSurfaceUnlock, déjà tenu) : jusqu'à 3,4 ms en jeu et 11,8 ms dans l'interface lourde. Risque : image périmée si un programme écrit sans verrou.
4. **Présentation asynchrone** (SwapEnd et attente GPU hors du fil de rendu) : 2,7 à 5,6 ms en jeu, jusqu'à 11 dans l'interface. Demande trois surfaces d'affichage et de vrais compteurs d'usage. Risque élevé : déchirures.
5. **Processeur** : fil de rendu et fil du jeu sur M4/A75, puis revoir le thermique quand 1-3 auront réduit la chauffe. Ne pas relever le seuil avant.
6. **Mesurer les 14 ms du jeu** (environ 260 µs par appel de dessin) : le profileur ne suit que backboardd et les compteurs d'émulation PAC sont compilés éteints.
7. **Build de performance sans diagnostics** : profileur à 420 relevés/s, journal écrit sur disque chaque seconde, une ligne `createport` par image. Gain non mesuré.

Ordre conseillé : 1, 2, 3, puis nouvelle mesure avant 4 et 5.

**Mise à jour 0350** : 1 fait (30 → 46 produites, 39 affichées, voir `build-b1l-0350.md`). La copie GPU subit elle aussi deux nettoyages par image : passer 3 avant 2.

**Mise à jour 0351** : 3 fait (jeu : 57 produites, 38,5 affichées ; interface : nettoyage médian 153 → 5 ms par 10 s, voir `build-b1l-0351.md`). Le plafond du jeu est maintenant backboardd : retirer d'abord la lecture de diagnostic de 0350 qui annule une partie de 3, puis 2, puis 4. (Lecture retirée en 0352, non testé.)

**Mise à jour 0352** (testé) : 58-60 i/s produites et 57-58 affichées dans les scènes simples. Scènes chargées : 29-32, avec 17 à 24 ms de GL par image pour 57 à 74 dessins. La limite y est le jeu lui-même : la piste 6 (séparer processeur et GPU dans ces images) passe avant 2 et 4. Rien d'engagé : travail arrêté après 0353.

## Défauts vus en passant
- `trapUseCount` ne compte rien : `IOSurfaceIsInUse` est toujours faux, l'anneau de trois surfaces du jeu n'en utilise que deux (36 et 37, 149 fois chacune). À corriger avec 1 ou 4.
- Le vsync est une minuterie logicielle de 16 667 µs non calée sur la dalle : saccades possibles, mais ce n'est pas la cause des 30 i/s.
