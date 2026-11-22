# B1L-0314 — Sur quels cœurs tournent les fils de backboardd (noyau, set v149)

## Pourquoi

Le fil d'affichage de CoreAnimation est le frein (0312). Le S10 a 4 petits cœurs A55 (1,85 GHz ici), 2 moyens A75
(2,21 GHz) et 2 gros M4 (2,08 GHz ici). Il faut savoir où ce fil tourne.

## Changement

`b2l_prof.c` : chaque échantillon d'un fil est compté par grappe (cœurs 0–3 A55, 4–5 A75, 6–7 M4). La ligne
`B2L PROF T` se termine par `on A55/A75/M4 a/b/c`.

## Résultat

Flashé (BOOT `5df5865d…`), puis remplacé aussitôt par 0315, qui le contient.
