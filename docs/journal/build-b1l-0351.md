# B1L-0351 — nettoyer le cache seulement après le processeur (noyau seul, set v166, GL v13)

**Demande.** Étape 3 de `analyse-framerate-0349.md` (« Oui, étape 3 »).

**Cause visée.** `flushBeforeJob` nettoyait toute la surface avant chaque tâche GPU qui la cite : 11,8 ms par image dans l'interface lourde (environ 31 fois par image), environ 3,4 ms à la copie GPU du jeu et autant dans backboardd.

**Règle.** Une surface est nettoyée si le processeur a pu y toucher depuis le dernier nettoyage :
- `IOSurfaceLock` et `IOSurfaceUnlock` (compteur `cpuUses`) ;
- copie du noyau vers la dalle (même compteur) ;
- mémoire mappée comme tampon Mali, donc écrite sans verrou (`cpuMapped`) : nettoyée à chaque tâche, comme avant, et journalisée.

Le nettoyage invalidait aussi les lignes pour qui relit ce que le GPU a dessiné. S'il a été sauté, le prochain verrou en lecture seule le fait (`HakiosMaliCpuReads`).

`mali 0x7d`, `WAIT_BO`, `HakiosMaliDrawn` et les surfaces `flushOnce` sont inchangés.

**Vérifié dans l'IOSurface d'Apple 23G90 (cache partagé, désassemblé).** `IOSurfaceClientLock` et `Unlock` appellent toujours les trappes 2 et 3, sans chemin rapide. `LockPlane`, `UnlockPlane` et `FlushProcessorCaches` passent par eux. Les trappes 8 (retrait de clés CoreVideo) et 9, refusées par notre noyau, n'écrivent pas de pixels.

**Fichiers.** `B2LSurface.h`, `B2LSurface.cpp`, `B2LMaliService.cpp`, `B2LFramebuffer.cpp` (sauvegardes `.before-0351`).

**Vérifié sur le Mac.** `tests/test-surface-flush.py` exécute les fonctions du fichier : règle, lecture, surfaces mappées, débordement du compteur, 20000 courses tâche/déverrouillage, coupure. `test_surface_properties.py` ne compile plus depuis un changement antérieur (`b2l_surf_log_op` manque dans son harnais) : passe avec cette fonction ajoutée dans une copie.

**Risques.** Un programme qui écrit sans verrouiller donnerait une image périmée ou des lignes parasites. Une écriture partielle verrouillée juste après un dessin du GPU pourrait, rarement, reposer sur des lignes lues par spéculation.

**Coupure.** `b2l_flush_lazy=0`. Les arguments de démarrage sont figés dans `XnuLoader.c` : il n'y a pas de réglage depuis le téléphone. Le plus court est de mettre `gMaliFlushLazy = 0` et de reconstruire le noyau.

**Flash.** BOOT `ea24a527…` (BOOT-OK), SYSTEM-OK, les trois objets recompilés.

**À lire au prochain journal.** `B1L-0351 surfaces cleaned after the processor's use`, `surface N mapped as a buffer`, `flush N x, ms` par fenêtre, i/s produites et affichées (0350 : 46,3 et 38,9), envoi (6,8 ms, attendu vers 3,4).

**Résultat (testé le 2026-11-22, `out/logs/0351-boot.txt` et `0351-unified.logarchive`).** Vu par le propriétaire : jeu stable, pas beaucoup plus fluide ; capture d'écran possible ; galerie vide, photo visible en la touchant ; Photos plante à l'édition ; scintillements du Liquid Glass inchangés ; Safari lent mais marche.

Interface, hors jeu (fenêtres de 10 s, usages non identiques entre les deux démarrages) : nettoyage médian 153 → 5 ms, maximum 2768 → 62 ms (0349 → 0351).

Jeu (7 fenêtres ; i/s produites interpolées sur le compteur d'images) : 56,9 i/s produites en médiane (46,3 en 0350), 38,5 affichées (38,9). Envoi 4,3-4,9 ms (6,8). backboardd affiche environ deux images sur trois.
- Sa dépense par image affichée, lue à la fenêtre de la ligne 13208 : libération 2,6 ms, attente 2,6, soumission 2,5, imports 1,9.
- Défaut de 0350 révélé ici : la lecture du « middle pixel » (présentation 1 puis toutes les 600) précède immédiatement les deux seules lignes `mapped as a buffer` du démarrage (surfaces 42 et 44, deux des trois du jeu). Ces surfaces sont ensuite nettoyées avant chaque tâche : il reste environ 2,35 nettoyages et 3,7 ms par image produite. Que cette lecture soit la cause est déduit de l'ordre des lignes. Correctif : la retirer.

Photos, plantage (prouvé, deux rapports identiques, pids 428 et 479) : Vision, détection de visages → Espresso → MPS → `failed assertion ... unable to load function cnnConvArray_3xIn_16xOut_1_2`, `intrinsic: air.is_uniform.i16 (block 0, instruction 5, opcode 17)`. Notre traducteur AIR n'a pas cette fonction. hakios-metal l'a (`window-server/tools/mesa/air/air_intrinsics.c:1314-1326`, MIT, `nir_vote_ieq` / `nir_vote_feq`), testée par `tools/kernels/uniform_id.ll`. D'autres refus peuvent suivre : le traducteur s'arrête au premier.

Galerie vide (cause probable, pas prouvée), deux refus du pilote :
- texture 4092 x 992 au format 204 (ASTC 4x4) refusée, 6 fois ; Mesa connaît ASTC pour panfrost, notre table Metal non ;
- `fragment_main reads buffer 5 and 3 buffers were bound` : 295 dessins abandonnés. Le premier compte 54 sommets en plein écran, juste après le refus ASTC. Lien entre les deux inconnu ; lire le shader avant de corriger.
- Aussi refusé : import IOSurface 420f à deux plans, et imports dont la taille demandée est plus petite que la surface.

0351 en cause ? Pas pour le plantage : refus du traducteur du set v166, que ce build ne change pas. Pour la galerie, les refus viennent du même set et aucune surface de Photos n'est marquée mappée, mais il n'existe pas de journal antérieur de la galerie pour comparer, donc pas de preuve absolue.

Non expliqué : scintillements du Liquid Glass, lenteur de Safari (non analysés ici). Vu en passant : fileproviderd abandonne dans `-[FPDDomain volume]`.
