# B1L-0293 — tampons GPU mappés d'un coup (`kIOMapPrefault`, noyau seul)

## Pourquoi

0292 : le fil de rendu de backboardd passe ~75 % de son temps dans le noyau, sur ~30 000 défauts de page par seconde,
dans la mise à zéro et les copies de Mesa sur des tampons tout juste remappés (voir build-b1l-0292.md).

## Changement (un seul)

`B2LMaliService.cpp`, `clientMemoryForType` : pour nos propres tampons (IOBufferMemoryDescriptor non paginables, donc
préparés et câblés à la création : `kIOMemoryAutoPrepare`, IOBufferMemoryDescriptor.cpp), le mappage demandé par
Mesa (`IOConnectMapMemory64`) prend `kIOMapPrefault` (IOMapTypes.h, géré par IOMemoryDescriptor.cpp) : toutes les
pages sont posées dans la table du programme au mappage, au lieu d'un défaut par page. Les IOSurfaces importées
gardent l'ancien chemin. Pas de changement de pilote Metal (`--no-libs`, set v139).

Pourquoi pas « keepmap » (garder le mappage, G1) : en 0285/0286 il a fait manquer la mémoire ; le noyau reprend les
tampons du cache (B1L-0258) et un mappage gardé empêche peut-être de rendre leur mémoire. Ici rien n'est gardé de plus.

## À mesurer

`B2L PROF FAULTS` (272 000–325 000 par 10 s en 0292), part noyau du fil `com.apple.coreanimation.display`,
`B2L PROF ... swaps` (i/s), plantages, « no memory ». Ressenti du propriétaire.

## Résultat

Défauts de page **272 000–325 000 → 67–2 093 par 10 s** ; fil `com.apple.coreanimation.display` : part noyau ~90 % →
~62 %. i/s 9–23 sur 10 s : le propriétaire ne sent pas de grande différence. 0 plantage `code=0xa`, mais 34
« no memory » (tampons de 35 Mo, avec 1,6 Go libres) et **Safari souvent blanc** : depuis 0292, `com.apple.WebKit.GPU`
a le Mali (relancé 10 fois ; il dessine tracés et copies d'images, `path_cubic`, `buffer_image_blit`). Spotlight a aussi
demandé le GPU. Suite : 0294.
