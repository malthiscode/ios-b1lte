# B1L-0341 — Émulation PAC aussi pour les apps arm64

## Pourquoi

0340 : Geometry Dash (binaire arm64, pas arm64e) plante au lancement : `sending exception 2 (0x1 0xd65f0fff)`.
`0xd65f0fff` est `RETAB`, une instruction PAC des bibliothèques arm64e du cache dyld d'iOS. Une app arm64 utilise
ce même cache. Le Exynos 9820 n'a pas PAC : le noyau émule ces instructions (B1L-0218), mais seulement pour les processus
arm64e (`b2l_arm64e_current_task`).

## Changement

`xnu/bsd/dev/arm/kern_machdep.c` et `patches/0006-b2l-arm64e-compat.patch` (sauvegarde `.before-0341`) :
`b2l_arm64e_current_task` est vrai pour tout processus `CPU_TYPE_ARM64`. En mode « stripped », l'émulateur traite `RETAB`
comme `RET` et les AUT/PAC comme l'identité : c'est ce qu'un processus arm64 attend (clés PAC désactivées).
Le code propre d'une app arm64 ne contient au plus que des alias PAC de l'espace HINT, qui s'exécutent comme des NOP sans piège.
Noyau seul, set v164.

## À vérifier

- plus d'exception 2 sur GeometryJump ; le jeu s'ouvre-t-il (OpenGL ES) ?
- aucune régression des apps arm64e.

## Résultat

BOOT-OK (boot 2ae895c6…). Plus de piège PAC. **Le jeu ne s'ouvre toujours pas** : SIGSEGV à l'adresse 0 (rapport `GeometryJump-2026-11-22-073721.ips`, lu en lecture seule par `pull-b2l-unified-logs.py --include-prefix …/CrashReporter`). Pile : `_platform_strlen` ← GeometryJump 0x9924 (constructeur de `std::string`) ← 0x324a24, qui passe le résultat de `glGetString(0x1F00)` (`GL_VENDOR`). `glGetString` rend NULL : aucun contexte OpenGL ES. Sur iOS, OpenGL ES repose sur le pilote GL des GPU Apple (AGX), absent ici. Le pont EAGL de HakiOS (`build-gl/src/eagl.c`, B1L-0031) ne sert que le serveur de fenêtres : il n'a ni `renderbufferStorage:fromDrawable:` ni `presentRenderbuffer:` pour un `CAEAGLLayer` d'app.
