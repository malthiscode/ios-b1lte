# B1L-0305 — Cache de tampons de Mesa plafonné à 160 Mo (Mesa, set v145)

## Pourquoi

0303 : la mémoire GPU de backboardd grimpe de 344 à 1126 Mo, dont 623 Mo dans le cache de tampons de Mesa. Depuis 0302,
le noyau ne reprend plus ces tampons tant que de la mémoire reste disponible. Le propriétaire voit « fluide au début, puis
beaucoup plus lent ». (En 0291, cache ~185 Mo et 59,8 i/s.)

## Changement

`apply.py`, bloc B1L-0305 sur `pan_bo.c` : compteur des octets du cache (sous `bo_cache.lock`), mis à jour aux 5 endroits
où un tampon entre ou sort ; au-delà de **160 Mo**, les plus anciens sont libérés (`panfrost_bo_cache_put`). La règle de
Mesa (libéré après 2 s) reste. Testé à sec sur une copie de `pan_bo.c`. Raccourci noté : un compteur par processus (un
seul appareil panfrost par processus aujourd'hui). Set v144 → v145.

## À vérifier

`mali mem: backboardd` stable (~400–600 Mo, pas 1,1 Go) ; i/s qui ne baissent plus avec le temps ; créations
(`B2L CREATE`, 0x42) pas beaucoup plus hautes ; 0 « no memory ». Premier démarrage : cache de shaders refait
(~50 s figé possible, P13).

## Résultat

Premier essai : la compilation de `pan_bo.c` échoue (`uint64_t` pas encore déclaré en haut du fichier) → compteur en `unsigned long long`. Flashé (BOOT `ab81d280…`, BOOT-OK). **Propriétaire : ça reste correct dans la durée, ~25 i/s, pas extrêmement fluide.**
Log : cache de Mesa plafonné (purgeable 1 à 120 Mo au lieu de 623) ; mémoire GPU de backboardd encore jusqu'à 871 Mo
(tampons vivants) ; i/s 6 à 24 par fenêtre ; 4 « no memory ».
