# B1L-0290 — G2 : cache de shaders en un seul fichier (set v138, Mesa)

## Pourquoi

0289 : 6,7 s par démarrage à lire le cache de shaders, la clé 2 ms. La base par défaut de Mesa
(`util/mesa_cache_db*.c`) est coupée en 50 morceaux. Chaque recherche pose des verrous de fichier, relit des
en-têtes, peut parcourir plusieurs morceaux, et **réécrit l'heure d'accès avec un `fflush` à chaque lecture réussie**.

## Changement (un seul)

`apply.py` section `B1L-0290` dans `air_metal.c` : `MESA_DISK_CACHE_SINGLE_FILE=1`, le cache « un seul fichier » de
Mesa (`util/fossilize_db.c`, code de Mesa, rien d'inventé). Son index est gardé en mémoire : une lecture réussie =
un `fseek` + un `fread` sous un verrou interne, sans verrou de fichier ni écriture. Il a son propre dossier
(`/b2l-dump/mesa-cache/mesa_shader_cache_sf`). Set v137 → v138. Les compteurs de 0288/0289 restent pour mesurer.

## Limite connue

Ce cache n'a pas de taille maximale (quelques Mo par build du pilote). À vider si la place manque.

## À mesurer (deuxième démarrage)

`read N ms` dans `B2L SHADERCACHE`, à comparer aux 6716 ms de 0289 ; total `rwa_render_create shader compile`.

## Résultat

Deuxième démarrage (propriétaire) : `473 found (107 ms), 2 compiled (6 ms); key 2 ms, read 98 ms`. **La lecture passe de
6716 ms à 98 ms** (~0,2 ms par shader). Total `rwa_render_create shader compile` : 3,5 s (6,7 s en 0288) ; le reste est la
traduction AIR → NIR. 0 « no memory », 0 `code=0xa`, pilote v138 chargé. **G2 terminé.**
