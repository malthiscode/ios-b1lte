# B1L-0334 — Mémoire de tuile coupée (userspace, set v161)

## Pourquoi

0333 : le propriétaire signale encore des coins jaunes, du verre blanc et des clignotements. Le log montre encore
4391 attachements memoryless invalidés par 10 s. Les coins jaunes sont apparus avec 0329 (invalidation après la passe).

## Changement

`hakiosmetal_air.m` (sauvegarde `.before-0334`) : l'appel à `mesa.texture_discard` après la dernière passe est désactivé
(`if (0 && …)`). On revient au comportement d'avant 0329. Le reste de 0329/0330 (pool, éviction) reste en place.

## À vérifier

- plus de coins jaunes ; « not written back » à 0.

## Résultat

BOOT-OK (boot 4bad205b…), flashé, suivi tout de suite de 0335 : le propriétaire teste les deux ensemble.
