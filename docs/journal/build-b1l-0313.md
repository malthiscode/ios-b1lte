# B1L-0313 — Retour à la présentation synchrone (noyau, set v149)

## Pourquoi

0312 a rendu SwapEnd asynchrone. Il n'y a eu aucun gain d'i/s, et l'écran a montré des blocs noirs, des clignotements
et des déchirures : CoreAnimation réutilisait une surface encore affichée.

## Changement

`B2LFramebuffer.cpp` remis tel qu'avant 0312 (`.before-0312`).

## Résultat

Flashé (BOOT `5fa47683…`). Le propriétaire : plus de défaut visuel. Reste, comme depuis longtemps, le Liquid Glass trop blanc et opaque sur un fond d'écran clair (dossiers de l'écran d'accueil).
