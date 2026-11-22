# B1L-0343 — `+setCurrentContext:` pour les apps

**But.** En 0342, `+[EAGLContext setCurrentContext:]` répondait NO : la clé pthread d'OpenGLES n'était jamais créée (voir build-b1l-0342.md). cocos2d libérait alors le contexte, et `glGetString` rendait NULL.

**Changement.**
- `userspace/b2l_eagl_app.m` : `+setCurrentContext:` est remplacée. Elle lit `_private` (décalage 8) puis fait `pthread_setspecific(30, P)`. C'est la seule écriture que lisent les `gl*` et `+currentContext`. Elle répond YES si cette écriture réussit.
- Bibliothèque renommée `b2l-gl-v11` : le noyau ne remplace pas un fichier déjà publié. Les chemins sont mis à jour dans `b2l_exec_graphics_diag.h`, `b2l_userspace_seed.h` et `scripts/build-gl-app.sh`.
- SYSTEM : `system-add-fast.sh b2l-gl-v11`.

**Flash.** BOOT `c063a051…`, SYSTEM-OK.

**Résultat.** Le jeu dessine correctement sur le Mali (environ 5000 images en 80 s, 11 ms par image, rendu 3040 × 1440). Deux problèmes restent :
- Lancé depuis l'icône, il ne s'affiche pas : l'icône clignote et on revient à l'accueil. Le journal de SpringBoard montre un lancement normal : rotation en paysage, scène « ready » 4,2 s après le toucher. On ne voit pas d'abandon.
- Ouvert depuis le sélecteur d'apps, le jeu s'affiche mais reste bloqué sur son menu animé, et l'image va et vient. Avec deux surfaces, la moitié des images était écrite dans celle qui était encore à l'écran.
- **Rectificatif (0344)** : l'explication par les deux surfaces était fausse. La cause des deux problèmes est le refus, par notre Metal, des cibles d'écran tournées de backboardd (build-b1l-0345.md).
