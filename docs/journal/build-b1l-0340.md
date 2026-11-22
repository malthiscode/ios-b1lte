# B1L-0340 — Geometry Dash : le noyau cherche les magasins LaunchServices

## Pourquoi

0339 : l'app est dans `/Applications`, mais lsd garde son ancien magasin, d'où l'absence d'icône. Les noms et le
dossier de B1L-0025 (`InternalDaemon/5FD6E102-…/Library/Caches/com.apple.LaunchServices-20971545-v2.csstore`)
ne correspondent plus : `absent/kept`.

## Changement

`xnu-overlay/bsd/kern/b2l_app_install.h` : `b2l_app_ls_reset`, appelé une fois après `b2l_app_install`
(`b2l_userspace_seed.h`, sauvegarde `.before-0340`). Le noyau parcourt tous les conteneurs
`/private/var/mobile/Containers/Data/InternalDaemon/*/Library/Caches/` (`VNOP_READDIR`) et renomme chaque fichier dont le nom
contient « LaunchServices » et finit par « .csstore » en `*.b2l-before-0340` (rien n'est supprimé). Chaque fichier est journalisé
(`B2L APP LS:`). Le marqueur `/private/var/db/b2l-app-ls-reset-v1` n'est écrit que si au moins un fichier a été renommé.
Noyau seul, set v164.

## À vérifier

- `B2L APP LS:` : magasins trouvés et renommés ;
- l'icône Geometry Dash, et les autres apps toujours présentes ;
- le jeu se lance-t-il ?

## Résultat

BOOT-OK (boot 48717900…). Log : 2 magasins trouvés dans `InternalDaemon/09D5CCB5-…/Library/Caches/` et renommés ; lsd a reconstruit. **Geometry Dash apparaît et se lance, puis plante (7 fois)** : exception 2 (instruction illégale) sur `0xd65f0fff` = `RETAB`. → 0341.
