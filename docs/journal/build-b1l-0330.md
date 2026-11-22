# B1L-0330 — Réserve de textures : garder les cibles plein écran (userspace, set v158)

## Pourquoi

0327 : 13 277 textures jetées par la réserve en une session, 32 refus « no memory » pour des cibles de 26–36 Mo
avec 500–950 Mo libres. La réserve (128 Mo) ne tenait que trois cibles RGBA16F plein écran : chacune était jetée
puis refaite (30 Mo à allouer), ce qui fragmente la plage du noyau.

## Changement

`hakiosmetal_air.m` (sauvegarde `.before-0330`) : réserve 64 → 128 entrées, 128 → 320 Mo. Pleine par le nombre,
elle jette d'abord la plus ancienne des petites textures (< 8 Mo) ; pleine par la taille, la plus ancienne de toutes.
L'âge limite (1 s) ne change pas : la mémoire gardée reste celle d'images récentes.

## À vérifier

- « evicted » et « no memory » en baisse ; mémoire libre suffisante.

## Résultat

Flashé (BOOT `a940e444…`, BOOT-OK). Pas redémarré : 0331 par-dessus.
