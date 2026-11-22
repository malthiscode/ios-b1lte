# B1L-0350 — le jeu présente son image par le GPU (GL des apps v13, set v166)

**Demande.** Étape 1 de `analyse-framerate-0349.md` (« Oui, commence »).

**Cause visée.** À chaque image, `presentRenderbuffer` relisait 17,5 Mo (`glReadPixels`) puis les retournait et les convertissait au processeur, environ 11 à 17 ms déduits de la cadence.

**Changement.** `userspace/b2l_eagl_app.m` (sauvegarde `.before-0350`) :
- une texture sur la mémoire de chacune des trois IOSurfaces (`hakios_gl_surface_texture`, déjà présent), lignes alignées sur 64 octets ;
- `glBlitFramebuffer` retourné, scissor coupé puis remis, puis `glFinish` ;
- anneau réparti : la surface qui suit la dernière montrée (avant : deux sur trois, `IOSurfaceIsInUse` répond toujours non) ;
- si une surface est refusée, ancien chemin par le processeur ;
- journal toutes les 600 images : chemin, pixel central de la surface, durée moyenne et pire.

Noyau : seul le nom `b2l-gl-v13` change (deux en-têtes). Mesa v9 inchangé.

**Vérifié sur le Mac.** `tests/test-eagl-present.py` exécute les fonctions du fichier sur l'OpenGL du Mac : orientation, couleurs, alpha, scissor, choix de surface. Il échoue si le retournement ou la coupure du scissor est retiré. Les imports GL viennent de libOSMesa. Les journaux 0046 montrent ce Mesa copiant par le GPU dans une IOSurface, sur ce téléphone, pour backboardd.

**Non vérifié.** Import des surfaces depuis le processus du jeu, couleurs sur le Mali, gain réel. Le compteur d'usage du noyau reste factice : l'anneau espace les réécritures sans garantir qu'une surface est libre.

**Flash.** GL v13 `10c6f336…` et BOOT `d35c11f0…` relus (SYSTEM-OK, BOOT-OK).

**À lire au prochain journal.** `presented by the GPU`, `present N: … by the GPU … us (worst …)`, trois ids de surface, `swaps …/s` en jeu. Pour mémoire, 0349 : 29,6 i/s.

**Retour arrière.** Remettre les fichiers `.before-0350` (pont, deux en-têtes, script) et reconstruire le noyau : la v12 est toujours sur SYSTEM.

**Résultat** (`out/logs/0350-boot.txt`, epoch 359, jeu au premier plan environ 30 s).
- Copie GPU active (`presented by the GPU`), trois surfaces parcourues à parts égales (355, 356, 354 envois), aucun repli.
- Seule fenêtre de 10 s entièrement en jeu (lignes 10874-11810) : **46,3 i/s produites, 38,9 affichées** ; 0349 : 29,6 et 29,6. Le nombre d'images produites est interpolé depuis les lignes `frame N` (une sur 20) ; la même méthode redonne 30,0 sur 0349.
- Envoi : **6,8 ms** en moyenne sur 600 images, 28,7 au pire. Environ 3,4 ms sont deux nettoyages de cache de la surface (1,69 ms chacun, une fois par tâche GPU de la copie), attribution déduite du compte des nettoyages.
- Nouveau plafond, backboardd : environ 16 % des images produites ne sont pas affichées. Par image affichée : démontage 2,3 ms et réimport 2,1 (compteurs communs), nettoyage environ 3,4, SwapEnd 2,2.
- Scènes à 50 appels de dessin : 15,3 ms de GL. Avec l'envoi, plus de 16,7 ms : 60 i/s impossible là sans alléger le jeu lui-même.
- M4 de 1976 à 1664 MHz dans la fenêtre (72-84 °C).
- Stabilité : un seul backboardd, 0 faute GPU, 0 SIGSEGV ou SIGBUS, 0 copie écran, jeu repris après passage en arrière-plan.
- À surveiller : un pixel central d'alpha 0xca à l'image 600 (22 relevés sur 22 à 0xff en 0349). Un seul relevé, scène différente ; l'ancien chemin gardait aussi l'alpha. Déchirures et couleurs : non visibles dans un journal.

**Suite conseillée.** Étape 3 (nettoyage de cache) avant l'étape 2 : elle allège à la fois le jeu et backboardd.
