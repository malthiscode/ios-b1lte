# B1L-0344 — Anneau de trois surfaces et journal des touchers (Geometry Dash)

**But.** En 0343, l'image allait et venait, et le jeu semblait ne pas répondre (voir build-b1l-0343.md).

**Changement (`userspace/b2l_eagl_app.m`, bibliothèque `b2l-gl-v12`).**
- Trois IOSurfaces au lieu de deux. On n'écrit jamais dans la surface affichée, ni dans une surface encore utilisée par le compositeur (`IOSurfaceIsInUse`). On appelle `setContentsChanged` après chaque présentation.
  - Idée reprise de Jesvi-iOS, `userland/src/eagl_app.m` `present()` : licence MIT (« the HakiOS authors »), commit `1b41d5b`. Le code a été réécrit ici, pas copié.
  - Jesvi fait la même chose que nous (TSD slot 30, table à `P + 0x20`). Pour la copie, ils utilisent le GPU, avec `glEGLImageTargetTexture2DOES` sur un ID d'IOSurface, grâce à leur propre patch de Mesa. C'est une piste pour plus tard.
- Diagnostic : `-[UIWindow sendEvent:]` écrit dans le journal les 40 premiers touchers (« b2l-gl-app: touch ») avec leur position, la taille de la fenêtre, l'orientation et la vue touchée.
- Les chemins passent en `b2l-gl-v12` (noyau et script). SYSTEM : `system-add-fast.sh b2l-gl-v12`.

**Flash.** BOOT `25db42d2…`, SYSTEM-OK.

**Résultat.** Mêmes symptômes : **l'anneau de surfaces n'était pas la cause**, le diagnostic de 0343 était faux.
- Les touchers arrivent : 40 touchers journalisés vers `EAGLView` (fenêtre 1013x480, orientation 3).
- Cause, lue dans `out/logs/0344-boot.txt` : au lancement du jeu, backboardd demande des textures 3040x1440 sur les surfaces d'écran 2 et 3 (1440x3040), et notre Metal les refuse (`B2L IMPORT … -> refused`, 55 refus sur 64 demandes). Il continue pourtant d'échanger les deux tampons, et l'écran alterne deux images périmées.
- Correction : build-b1l-0345.md.
