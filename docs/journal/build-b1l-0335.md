# B1L-0335 — Cache de tampons Mesa : le dernier seau ne s'arrête plus au premier tampon occupé (Mesa, set v162)

## Pourquoi

Log 0333 (`B2L CREATE`) : en 10 s, backboardd crée 114 tampons de 4 Mo et plus (1937 Mo), ce qui coûte 946 ms
d'allocation et 256 ms de mappage, soit environ 12 % du temps. Les cadences tombent entre 16 et 37 images/s.
`panfrost_bo_cache_fetch` arrête la recherche au premier tampon occupé (« le plus vieux est occupé, les plus récents
aussi »). Cette règle ne tient que dans un seau d'une seule taille. Le dernier seau (4 Mo et plus) mélange toutes les tailles.

## Changement

`userspace/mesa/air-b2l/apply.py` (sauvegarde `.before-0335`), dans `pan_bo.c` : pour une demande de 4 Mo et plus,
- un tampon occupé est sauté (`continue`) au lieu d'arrêter la recherche ;
- un tampon de plus de deux fois la taille demandée n'est pas pris, pour ne pas gaspiller de mémoire.
Le plafond de 160 Mo (0305) et la règle des 2 s restent.

## À vérifier

- `B2L CREATE >=4M` : nettement moins de 114 par 10 s ; moins de ms d'allocation ; swaps/s plus élevés.

## Résultat

BOOT-OK (boot fb4a6e30…), Mesa recompilé avec le changement (vérifié dans pan_bo.c). Test du propriétaire à faire avec 0334.
