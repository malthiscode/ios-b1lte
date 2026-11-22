# B1L-0300 — Mesure : où va le temps d'une création de tampon (noyau, set v142)

## Pourquoi

0299 : la création de tampons (`mali 66`, `createBO`) coûte 1,4 à 2 ms chacune, 0,9 à 2 s par 10 s dans backboardd.

## Changement

`B2LMaliService.cpp`, `createBO` : trois temps mesurés, `purge` (reprise de tampons purgeables si < 256 Mo libres),
`alloc` (IOBufferMemoryDescriptor, par masque physique si ≥ 1 Mo, sinon par pages) et `map` (`newBuffer` : chaque page
de 4 Ko entrée dans les tables du GPU, verrou + vidage de cache par entrée), par taille (<64K, 64–256K, <1M, 1–4M,
≥4M). Ligne toutes les 10 s : `B2L CREATE: 10 s <taille>: N x, Mo, purge ms, alloc ms, map ms, physmask N`.
Mesure seule, noyau seul.

## Résultat

Flashé (BOOT `380b1191…`, BOOT-OK). 0300 et 0301 flashés à la suite, testés ensemble.

**Résultat (log 0301)** — par 10 s dans backboardd : tampons ≥ 4 Mo **24 à 110 créés, 290 à 1 330 Mo**, alloc 190–940 ms +
map 57–254 ms ; **purge 22 à 304 ms dans chaque taille**, à chaque création : la mémoire libre reste à 30–44 Mo
(`free now 30 MB`), toujours sous le seuil de 256 Mo de B1L-0258. Le noyau reprend donc sans cesse les gros tampons du
cache de Mesa, qui doit les recréer. → B1L-0302.
