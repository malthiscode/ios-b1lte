# B1L-0309 — Diagnostic des tampons de 36 Mo refusés : la plage d'adresses noyau (noyau, set v147)

## Pourquoi

0307 : 82 « no memory » pour des tampons de 36 175 872 octets, avec 640–680 Mo libres et 953 Mo de tampons GPU vivants.

Lecture de XNU :
- `createBO` passe par `IOBufferMemoryDescriptor`, puis `IOKernelAllocateWithPhysicalRestrict` (le masque 40 bits
  couvre toute la RAM, donc `maxPhys` = 0), puis `kernel_memory_allocate(KMA_DATA_SHARED)`.
- `vm_page_alloc_list` ne refuse 36 Mo que si la demande dépasse le quart de la RAM. Sinon il attend des pages, sans
  échouer.
- La limite « par extent » (`sane_size / 64`, au moins 2 Go) et la limite douce de mappage (2 Go) ne jouent pas ici.
- Reste la **plage d'adresses noyau « data shared »** : de taille fixe, elle est partagée par tous les tampons GPU et
  toutes les IOSurfaces. B1L-0121 l'a déjà portée aux deux tiers de l'espace data. Il lui faut un trou libre et
  contigu de 36 Mo.

## Changement

`B2LMaliService.cpp`, à l'échec de `createBO` : on demande la même taille en adresses seules (`KMA_VAONLY`), une fois
dans la plage partagée et une fois dans la plage privée, puis on rend les adresses aussitôt. Une ligne
`B1L-0309: addresses for N MB: shared range kr X (range M MB), private range kr Y (range M MB)` est écrite dans le
journal. kr 3 = `KERN_NO_SPACE`, c'est-à-dire plus d'adresses.

Ce build contient aussi 0308 (set v147). Il ne change que le noyau.

## À vérifier

- Si `shared kr 3` et `private kr 0` : la plage partagée est pleine ou fragmentée. Correctif : rendre les tampons plus
  tôt, ou agrandir la plage, ou placer les tampons GPU ailleurs.
- Si les deux donnent 0 : c'est autre chose que les adresses (pages).

## Résultat

Flashé (BOOT `3e1796eb…`). **Aucun « no memory » sur ce boot**, donc le diagnostic n'a rien écrit. Il reste en place pour la prochaine fois.
