# B1L-0348 — Affichage sans copie : la dernière couche qui couvre l'écran (noyau, set v165)

**Symptôme (`out/logs/0346-boot.txt`).** Geometry Dash à 12 i/s, image qui descend du haut de l'écran. 0 bascule, 124 copies en 8,7 s par 10 s : le processeur copie 17 Mo dans la mémoire en cours de balayage.

**Cause.** `swap()` refusait l'affichage sans copie. La condition en défaut n'est pas identifiée (l'enregistrement du jeu n'était pas journalisé). `whole` exigeait notamment un masque d'un seul bit, alors que QuartzCore envoie aussi des couches vidées, sans surface.

**Changement.** `B2LFramebuffer.cpp`, `B2LBlit.h` (sauvegardes `.before-0348`) : la copie ne mélange pas, donc seule la dernière couche visible compte si elle couvre l'écran. Elle est montrée sans copie. Un rectangle plus grand que l'écran est accepté, comme la copie le rogne. Une copie restante est journalisée : `B2L FB: B1L-0348 copy …` (8 premières, puis une sur 256).

**Vérifié.** Test hôte de la sélection, de la couverture et de l'égalité des pixels ; `tests/test_blit_scaled.py` passe. `tests/test_graphics_boundaries.py` ne compile pas (`planeCount` absent de sa fausse B2LSurface), sans rapport avec ces fichiers.

**À vérifier.** `zc flips` > 0 et `blit 0` pendant le jeu ; sinon lire la ligne `B1L-0348 copy`. Si des déchirures restent avec des bascules : regarder la bascule « une image en avance » de B1L-0110.

**Flash.** BOOT `dc600b1f…` (BOOT-OK), SYSTEM-OK.

**Résultat.** Testé. 0 copie sur tout le démarrage, jeu de 12 à 30 i/s. Aucune ligne `B1L-0348 copy` : la condition fautive était le masque ou un rectangle plus grand que l'écran, sans savoir lequel. Suite : `analyse-framerate-0349.md`.
