# B1L-0317 — La purge ne prend plus les tampons encore mappés (noyau, set v150)

## Pourquoi

0316 : 5 850 créations de tampons refusées (« no memory ») avec 3,6 Go encore disponibles. Les tâches GPU sont alors
lancées sans leurs tampons, d'où des fautes à l'adresse 0, du Liquid Glass noir ou absent, des icônes et des blocs
noirs.

Lecture du code :
- Depuis **B1L-0306**, un tampon qui part dans le cache de Mesa garde son mappage dans backboardd. Chaque
  `IOMemoryMap` tient une référence sur l'`IOBufferMemoryDescriptor` (`IOMemoryMap::init`, `fMemory`).
- La purge **B1L-0258** (`b2l_mali_purge`) prend ces tampons marqués DONTNEED, fait `memory->release()` et vide
  l'entrée. Le compteur « GPU buffers alive » baisse, mais le mappage retient encore les pages câblées et les adresses
  de la plage partagée.
- Plus tard, Mesa voit retained = 0, libère le tampon et le démappe (`IOConnectUnmapMemory64`).
  `is_io_connect_unmap_memory_from_task` demande d'abord `clientMemoryForType(handle)`. L'entrée étant vide, il ne
  trouve rien et le mappage n'est jamais retiré. Pages et adresses restent prises jusqu'à la fin de backboardd.
- Dans 0316, chaque refus déclenchait une nouvelle purge (2ᵉ tentative), donc d'autres fuites : de là les 5 850
  refus, et le câblé qui monte de 1,2 à 1,6 Go.

## Changement

`B2LMaliService.cpp`, `b2l_mali_purge` : un tampon dont le compte de références dépasse 1, donc encore mappé, n'est
plus purgé. Le cache de Mesa reste plafonné à 160 Mo (B1L-0258/0306).

Pas de diagnostic de la plage d'adresses : le `kernel_map` d'iOS 26 a des verrous par plage, et la sonde de 0309 était
soupçonnée du gel.

## À vérifier

- Beaucoup moins de « no memory », voire aucun ; pas de « fault » à l'adresse 0.
- Le câblé ne monte plus en continu.
- Liquid Glass, icônes et Calculatrice sans blocs noirs.
- Si des refus restent avant toute purge (le premier de 0316 venait avant toute purge, avec 1 023 Mo vivants),
  la cause est ailleurs (plage d'adresses) → à creuser.

## Résultat

Flashé (BOOT `e64641cd…`, BOOT-OK), téléphone redémarré.

- « no memory » : **5 850 → 464**. Plus aucune faute MMU du GPU.
- Les refus restants touchent de grandes cibles : 16 à 35 Mo (82 × 19,3 Mo, 61 × 35 Mo…). Ils viennent avec
  ~1,3 Go libre et **1,5 Go de tampons GPU vivants** (backboardd jusqu'à 867 Mo, cameracaptured 128 Mo), et des
  IOSurfaces jusqu'à 334 Mo.
- Textures : créations et libérations s'équilibrent, donc pas de fuite.
- Mesa vide déjà son cache et réessaie (`pan_bo.c`, `panfrost_bo_cache_evict_all`). La réserve de textures de
  libhakiosmetal (0311/0315, jusqu'à 128 Mo), elle, n'était jamais rendue dans ce cas. → 0318.
- i/s par 10 s : jusqu'à 48 (484 images en 10 s), le plus souvent 15–37.
