# B1L-0333 — Mémoire de tuile limitée aux textures memoryless (userspace, set v160)

## Pourquoi

0332 : coins jaunes autour des éléments de verre et du texte. panfrost garde dans UN lot toutes les passes qui
dessinent dans le même framebuffer ; invalider une cible `DontCare` à la fin d'une passe effaçait aussi ce qu'une
passe précédente du même lot y avait gardé (`Store`).

## Changement

`hakiosmetal_air.m` (sauvegarde `.before-0333`) : seules les textures memoryless (couleur et profondeur) sont
invalidées après leur passe ; l'action de stockage ne décide plus rien. Une texture memoryless ne peut être relue
après aucune passe (règle Metal), elle peut donc toujours être jetée.

## À vérifier

- plus de coins jaunes ; « not written back » toujours > 0.

## Résultat

BOOT-OK (SYSTEM 63 s, boot 8372a043…). Test visuel du propriétaire à faire (coins jaunes attendus disparus).
