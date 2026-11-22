# B1L-0298 — Mesure Liquid Glass par 10 s (set v141)

## Pourquoi

Le propriétaire trouve le Liquid Glass lent partout (Calculatrice, Réglages). Les lignes `B2L PASS` ne montrent que les
formes de passe **différentes** (dédoublonnées, 256 au plus) : elles ne disent pas combien de dessins de verre il y a.

## Changement

`userspace/metal/scion/hakiosmetal_air.m` : `b2l_lg_count()`, appelé pour chaque dessin (dans `b2l_note_pass`, avant le
dédoublonnage). Compte les dessins, ceux dont le shader de fragment contient `glass`, ceux qui contiennent `blur` ou
`downsample`, et la taille de leur cible couleur 0 (Mpx). Une ligne toutes les 10 s par processus, directement par
`kern.hakios.say` (pas limitée par le plafond de `rw_print`) :
`B2L LG: <processus> 10 s: N draws, G glass, B blur; targets X Mpx glass+blur, Y Mpx other`.
Mesure seule. Set v140 → v141, bibliothèques reconstruites (sans Mesa).

## À vérifier

Lignes `B2L LG: backboardd` ; comparer avec `B2L PROF swaps` et `B2L PERF submit` du même moment (Calculatrice ouverte).

## Résultat

Flashé (BOOT `0e5b3f1a…`, BOOT-OK). 0298 et 0299 flashés à la suite : le propriétaire teste les deux ensemble.

Lignes `B2L LG: backboardd` en régime normal (par 10 s) : **9 000 à 20 000 dessins** (~100 par image), dont **verre 3 à
1 800** et **flou 100 à 800**, soit 3 à 11 % des dessins et 3 à 11 % de la taille des cibles. Le Liquid Glass n'est
pas le poste principal : c'est le nombre total de dessins (souvent dans des cibles plein écran) qui pèse. Seuls backboardd
et cameracaptured ont demandé le GPU (Spotlight pas lancé).
