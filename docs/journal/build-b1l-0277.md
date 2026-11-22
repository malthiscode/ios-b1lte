# B1L-0277 — product-id du D94 montré seulement à audiomxd (+ Bluetooth)

## Pourquoi

Les os_log viennent de `out/live/ios26-userdata-audio-276.img`, lu en lecture seule.

- En 0275, avec `product-id` : audiomxd écrit « ProductID to int is: 43 ». VirtualAudio reconnaît donc l'iPhone 17,2 : c'est bien la bonne voie pour le son.
- Au même démarrage, backboardd s'arrête : « CoreAnimation: Failed to create SILManager, aborting... ».
  - Avec un vrai D94 reconnu, CoreAnimation attend le voyant sécurisé SIL (lié aux exclaves), et ses fichiers manquent (`secureindicatorassets`).
  - Ensuite : 3 SIGABRT, redémarrage de launchd, panic.
- En 0276, sans `product-id` : l'écran revient, mais audiomxd repasse à « ProductID to int is: 0 ».
- Jesvi-iOS (S20, iOS 16.5, iPhone X) copie toute l'identité de l'iPhone, `/product` compris. Sur iOS 16, il n'y a pas de SIL.

## Changement

- `IOUserClient.cpp` : nouvelle fonction `b2l_hide_product_id`, appelée dans `get_property_bytes`, `get_property`, `get_property_recursively` et `get_property_bin_buf`.
  - `/product` `product-id` reste lisible par audiomxd.
  - Les autres processus reçoivent « not found », comme avant 0275.
- `B2LPlatformExpert.cpp` : boot-arg `b2l_product_id`.
  - 0 : pas de `product-id`.
  - 1 : tous les processus (comme 0275, l'écran casse).
  - 2 : seulement audiomxd. C'est la valeur par défaut.
  - Journal : `B2L AUDIO: /product product-id D94 set, audiomxd only`.
- Bluetooth (session Bluetooth, noyau seulement) : contrôle `com.b2l.bluetooth`, termios, 0xfc18 à 115200, `transport-encoding` = 1. La dylib reste en v3 : rien de nouveau sur USERDATA.
- Le reste ne change pas : set Metal v133, plug-in audio v2, `acoustic-id` 8017, D94AP.

## Limite

- Une lecture du dictionnaire entier (`get_properties*`) montre encore `product-id` à tous. MobileGestalt la demande par son nom, donc ça suffit pour l'instant.

## À vérifier

- L'écran démarre (backboardd ne plante pas).
- `B2L AUDIO: /product product-id D94 set, audiomxd only`.
- `hakios: trace: start_io` / `do-io`, puis un son au clavier, à la sonnerie ou au verrouillage.

## Installé

- SYSTEM-OK, BOOT `361899421f5db50be15f8e8c2c8c0bf47dccf2cdf63d1cab68ed9469e6f1ff27`, BOOT-OK. Noyau non strippé : `out/mach-0277.unstripped`.
- Téléphone redémarré vers le système. Journal à tirer : `out/logs/0277-boot.txt`.
