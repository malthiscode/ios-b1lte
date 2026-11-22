# B1L-0339 — Geometry Dash (IPA du propriétaire) installé par le noyau

## Pourquoi

Demande du propriétaire : installer le plus simplement possible une app qu'il fournit (non distribuée ici). Accord
du propriétaire pour l'écriture dans USERDATA (2026-11-22).
L'app : Geometry Dash 2.208 (`com.robtop.geometryjump`), arm64, 6 201 fichiers, 171 Mo, OpenGL ES.
L'installeur d'iOS n'a jamais marché sur ce port (B1L-0013/0025). Une copie hors ligne dans l'image USERDATA demande `sudo`, exclu.

## Changement

- `scripts/stage-app.py` (nouveau) : range le .app à plat (`fNNNNN`) avec `manifest.txt` dans
  `build-metal/b2l-app-gd1/`, ajouté à SYSTEM par `system-add-fast.sh`.
- `xnu-overlay/bsd/kern/b2l_app_install.h` (nouveau), appelé par `b2l_userspace_before_init`
  (`b2l_userspace_seed.h`, sauvegarde `.before-0339`). Une seule fois, avant launchd :
  1. copie des fichiers vers `/private/var/b2l-app-gd1/GeometryJump.app` (reprise possible) ;
  2. renommage en `/Applications/GeometryJump.app` ;
  3. renommage des deux magasins LaunchServices (`*.b2l-before-0339`), que lsd reconstruit ;
  4. marqueur `/private/var/db/b2l-app-gd1`.
  Interrupteur : boot-arg `b2l_app_gd=0`.
- Fond d'écran : à la demande du propriétaire, son image sombre est forcée à nouveau (`B2L_FORCE_WALLPAPER 1`), et la bibliothèque est renommée `b2l-springboard-v7.dylib` (sauvegarde `b2l-springboard.m.before-0339`). PosterBoard garde le Mali.
- SYSTEM : `b2l-app-gd1` ajouté à part (`system-add-fast.sh`, 184 Mo écrits, SYSTEM-OK).
- Noyau seul (`--no-libs`), set v164 inchangé.

## À vérifier

- `B2L APP:` dans le log : nombre de fichiers, renommages, `error=0` ;
- l'icône Geometry Dash sur l'écran d'accueil ; le jeu se lance-t-il (OpenGL ES sur Metal) ?
- les autres apps sont toujours là après la reconstruction du magasin LaunchServices.

## Résultat

BOOT-OK (boot 7c78c0c2…), téléphone redémarré. **Propriétaire : le fond sombre s'affiche et le Liquid Glass est correct partout dessus.** Le verre blanc venait donc du fond clair, pas du pilote. Geometry Dash n'apparaît pas. Log : `GeometryJump.app -> /Applications moved`, 6201 fichiers, `error=0`, mais les deux magasins LaunchServices sont `absent/kept` (noms de B1L-0025 introuvables) : lsd n'a rien reconstruit. → 0340.
