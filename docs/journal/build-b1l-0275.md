# B1L-0275 — product-id du D94 (type de produit MobileGestalt pour le son)

## Pourquoi

- En 0274, `hw.model` vaut bien `D94AP`, mais audiomxd dit encore (os_log du 20/11, image `ios26-userdata-bt-274.img`) :
  - « ProductID to int is: 0 » ;
  - « ProductID is not defined in PlatformUtilities_iOS::GetProductType(), and AcousticID '8017' is not handled » ;
  - « Unknown product ID (0): unable to return a valid RoutingDatabase ».
  Résultat : aucun `start_io`, pas de son.
- `MGGetProductType()` (libMobileGestalt, cache 23G90) ne lit pas `hw.model`.
  - Elle lit `IODeviceTree:/product` → `product-id` (20 octets) et la compare à une table de 523 modèles.
  - Le code est à `0x1b22aeff8` ; la chaîne d'erreur est « invalid product id '%@' ».
- Notre XnuLoader crée `/product` sans `product-id`, donc MG renvoie -1, et VirtualAudio met ProductID 0.
- Le vrai D94 (DeviceTree.d94ap, 23G90) a `product-id` = `4fcb57a0bb4e68fc2ae70383735295e2a9bc6919`.
  - Dans la table MG, c'est l'entrée 70, type `0x13b8c89e`.
  - Dans VirtualAudio, ce type donne ProductID `0x2b`.
- La liste d'IDs acoustiques acceptée en secours (IsAcousticIDHandled) est 8010, 8021–8025, 12002–12003, 10012, 2025–2034 et 4013–4017. 8017 n'y est pas : il faut donc passer par le type de produit.

## Changement

- `B2LPlatformExpert.cpp` (bloc 0273) : pose `product-id` du D94 sur `/product`, seulement si la propriété est absente.
  - Ce n'est pas un identifiant propre au téléphone : c'est une valeur fixe et publique du modèle.
  - Journal : `B2L AUDIO: /product product-id D94 set`.
  - Coupure : `b2l_product_id=0`.
- Aucune nouvelle écriture USERDATA : MG lit `product-id` en direct, sans le cache. Pas de nouvelle version du cache.
- Le reste ne change pas par rapport à 0274 (set Metal v133, Bluetooth v3, plug-in v2).

## Risques

- MobileGestalt reconnaît maintenant un vrai iPhone 16 Pro Max. Des démons peuvent activer des chemins propres au D94 (écran, caméra, capteurs).
- Si l'écran ou le tactile cassent : reconstruire avec `b2l_product_id=0`.

## À vérifier

- `B2L AUDIO: /product product-id D94 set` dans le journal.
- `hakios: trace: start_io` et `do-io` (plug-in v2), puis un son au clavier, à l'aperçu de sonnerie ou au verrouillage.
- Dans os_log : « ProductID to int is: 43 ».

## Installé

- Build commun : audio (`product-id`) + Bluetooth (`b2l_amfi_constraint.h` : politique dyld 0x63 pour BlueTool et bluetoothd, interposition autorisée).
- SYSTEM-OK, BOOT `e57d2ffa2ed89cb285848e5aea7e7d4af28b0a4bcda66e3f315d76c8d5362c78`, BOOT-OK. Noyau non strippé : `out/mach-0275.unstripped`.
- Téléphone redémarré vers le système. Journal à tirer : `out/logs/0275-boot.txt`.
