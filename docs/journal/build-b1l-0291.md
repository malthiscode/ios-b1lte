# B1L-0291 — tampons repris par le noyau : répondus « libres » (noyau seul)

Nouveau but du propriétaire (2026-11-21) : généraliser l'accélération GPU à tout le système, gestion mémoire et fluidité,
viser 60 i/s.

## Constat (log 0290)

- GPU occupé 6 à 12 % seulement : le frein est côté processeur (`commit` ~10 ms, jusqu'à 201 ms) et dans le log.
- backboardd : 908 Mo en 711 tampons, dont **535 Mo « purgeables »** (cache de tampons de Mesa).
- **3412 attentes de tampon refusées** (`b2l-drm: request 0x41 -> 0xe00002f0`, kIOReturnNotFound). Le noyau reprend les
  tampons purgeables quand la mémoire baisse (B1L-0258, seulement s'ils sont inactifs) ; ensuite `WAIT_BO` sur ce numéro
  répond « introuvable », Mesa (`panfrost_bo_wait`) le prend pour occupé et le garde dans son cache, et redemande à chaque
  recherche. Chaque refus = un appel noyau + une ligne de log.
- Correction du plan : le tas du tiler ne coûte que **16 Mo** réels par processus (`heap of 128 MB addresses, 16 MB of
  memory behind them`) ; « 128 Mo » dans `mali mem` est la taille d'adresses. G3 rapporterait peu.

## Changement (un seul)

`B2LMaliService.cpp` : `WAIT_BO` sur un tampon repris (`fBuffers[h-1].purged`) répond « fini », comme Linux (un tampon
vidé reste un numéro valide jusqu'à `GEM_CLOSE`). Mesa demande alors `MADVISE`, voit `retained = 0` et libère le tampon
(`panfrost_bo_cache_fetch`). Build `--no-libs`, set v138 inchangé.

## À mesurer

Lignes `b2l-drm: request 0x41 ... 0xe00002f0` (3412 en 0290) → ~0 ; `mali mem` de backboardd ; plantages, « no memory ».

## Résultat

Log du propriétaire : attentes refusées **3412 → 0** ; mémoire GPU de backboardd **908 → 615 Mo**, dont purgeable
**535 → 185 Mo** ; 0 plantage, 0 « no memory », 0 `code=0xa`. Images par seconde (lignes `B2L PROF ... swaps`, compteur
déjà en place, F0 n'a pas besoin de build) : jusqu'à **52,7 et 59,8 i/s** sur 10 s (19 au plus en 0290) ; les fenêtres
lentes (6 à 23 i/s) sont celles où le processeur passe 2 fois plus de temps dans le noyau que dans les programmes.
