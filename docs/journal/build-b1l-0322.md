# B1L-0322 — Plage partagée : les gros tampons depuis la fin, les petits depuis le début (noyau, set v152)

## Pourquoi

0321 et avant (0307, 0317) : des refus pour les cibles plein écran de 19 à 36 Mo, avec 780 Mo libres et ~1 Go de
tampons GPU vivants, dans une plage d'adresses partagée de 2 880 Mo. Les tampons plus petits passent.

`kmem_find_space` place tout au premier trou libre depuis le début (`vm_map_store_lookup_hole_forwards`) :
- petits et gros tampons se mélangent ;
- les petits remplissent les trous laissés par les cibles plein écran libérées ;
- à la fin, il ne reste plus de trou de 36 Mo d'un seul tenant.

Effets visibles : blocs noirs, éléments qui clignotent, retours en arrière (une IOSurface montrée avec une image
ancienne).

## Changement

`xnu/osfmk/vm/vm_kern.c`, `kmem_alloc_guard_internal` : dans la plage partagée (`KMA_DATA_SHARED`), une allocation
de **1 Mo ou plus** prend `vmkf_last_free`. Elle est cherchée depuis la fin (`vm_map_store_lookup_hole_backwards`,
déjà utilisé pour les allocations permanentes). Les petites restent au début. Les allocations `KMA_PERMANENT` ne
changent pas.

## À vérifier

- « no memory » : 0 pour les cibles de 19–36 Mo.
- Plus de blocs noirs, de clignotements ni de retours en arrière.

## Résultat

Flashé (BOOT `9cd69b77…`, BOOT-OK), téléphone redémarré.

- « no memory » noyau : **127 → 13**.
- Mais l'espace d'adresses **GPU** est morcelé lui aussi : `cannot be entered into the GPU's address space` pour des
  tampons de 128 Mo (×14, le tas du tiler de chaque nouveau programme), 96 Mo (×4) et 36 Mo (×3). S'y ajoutent
  54 fautes GPU à l'adresse 0, sur des tâches restées sans leurs tampons. → 0323.
- **Réglages plante** 7 fois (SIGABRT). Sa tentative d'ouvrir le GPU échoue sur le tas de 128 Mo : pas d'écran
  panfrost, pas de périphérique Metal (« a buffer of 412 bytes needs a gpu client »), puis abort.
