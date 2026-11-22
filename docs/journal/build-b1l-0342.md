# B1L-0342 — OpenGL ES pour les apps (Geometry Dash)

**But.** 0341 : Geometry Dash passe le PAC puis plante dans `strlen(NULL)`. `glGetString(GL_VENDOR)` rend NULL car le pilote GL d'Apple (AGX) est absent.

**Ce qu'on a lu dans OpenGLES 23G90** (extrait du cache dyld, `otool`) :
- `+[EAGLContext setCurrentContext:]` met l'ivar `_private` (décalage 8) dans le TLS, slot 30.
  - Chaque fonction `gl*` appelle `entry(*(P+0x20), …)` à `P + 0x20 + décalage`. Exemple : `glGetString` lit `P+0x3d0`, soit l'entrée `0x3b0` de `eagl_slots.txt`.
  - `+currentContext` rend `*(P+0)`.
  - Elle lit aussi l'octet à `*(P+8)+0x18`. Si son bit 0 est à 0, elle appelle deux fonctions du moteur GL.
  - Le pont ne remplit pas `_private`. Avec le pont seul, une app aurait donc `gl*` → NULL, ou un plantage.
- `-initWithAPI:sharegroup:` (celui qu'utilise cocos2d) va à `-commonInitWithAPI:properties:`, pas à la méthode que remplace le pont. `-initWithAPI:` y va bien.

**Changement.**
- `userspace/b2l_eagl_app.m` : nouveau fichier, sous licence BSD-2.
  - Chaque contexte reçoit un `_private` : le contexte, un bloc d'indicateurs (bit posé) et une copie de la table du pont.
  - `initWithAPI:sharegroup:` est renvoyé vers `initWithAPI:properties:`.
  - `renderbufferStorage:fromDrawable:` donne au renderbuffer un stockage ordinaire (RGBA8) à la taille du calque en pixels.
  - `presentRenderbuffer:` (et ses variantes `atTime:` et `afterMinimumDuration:`) : `glFinish` du pont, puis `glReadPixels`, puis copie retournée et convertie en BGRA dans l'une de deux IOSurfaces. Cette surface devient le `contents` du calque.
- `scripts/build-gl-app.sh` produit `build-gl/b2l-gl-v10/libb2l-gl-app.dylib`.
  - Contenu : eagl.c, gl_osmesa.c et b2l_eagl_app.m.
  - Lié au `libOSMesa` de v9, déjà publié sur le téléphone.
  - Sans `b2l_accel_gles.c`, qui patche QuartzCore pour le serveur de fenêtres.
  - Le constructeur du pont s'exécute en premier (vérifié avec `dyld_info -inits`).
- Noyau :
  - `b2l_exec_glapp_inject` (`b2l_exec_graphics_diag.h`) insère la bibliothèque dans `…/GeometryJump.app/GeometryJump`. Interrupteur : `b2l_gles_app=0`.
  - `b2l_glapp_dyld_pid` est ajouté à la politique dyld d'AMFI.
  - `b2l_gl_publish` publie `/usr/local/lib/b2l-gl-v10/libb2l-gl-app.dylib`.
- SYSTEM : `system-add-fast.sh b2l-gl-v10`.

**Flash.** BOOT `85912cab…`, SYSTEM-OK.

**À regarder au test.**
- Le jeu affiche-t-il quelque chose ?
- Lignes du journal : `B2L GLES APP:`, `b2l-gl-app:`, `hakios-gl:`. Les lignes de l'app passent par `kern.hakios.say`, que le bac à sable peut refuser.
- Rapport de plantage s'il y en a un.

**Limite connue.** Une relecture complète par image, donc lent.
- Étape suivante si ça dessine : renderbuffer en mémoire d'IOSurface (`hakios_gl_surface_texture`).

**Résultat.** La bibliothèque est injectée et le contexte est créé sur le Mali (« OpenGL ES 3.1 Mesa 25.0.7; Mali-G76 (Panfrost) »). Le contexte est ensuite libéré aussitôt, puis on retrouve le même `strlen(NULL)` qu'en 0341.
- Cause : `+setCurrentContext:` (`eagl_ctx_tls_set`) fait un deuxième `pthread_setspecific` avec une clé créée par `eagl_init` (un `pthread_once`). Cette clé n'est jamais créée, puisque le pont court-circuite `-commonInitWithAPI:`. La méthode répond donc NO, et cocos2d libère le contexte.
- Correctif : voir 0343.
