# B1L-0276 — retour de l'écran : product-id coupé par défaut

## Pourquoi

- En 0275, le téléphone ne démarre plus vers iOS.
  - backboardd plante 3 fois en SIGABRT (pids 63, 211, 240).
  - launchd redémarre (« critical process crashes: backboardd »), puis panic « Halt/Restart Timed Out ».
- En 0274, sans `product-id`, l'écran marchait. Le suspect est donc le `product-id` du D94 de 0275 : MobileGestalt reconnaît alors un vrai iPhone 16 Pro Max partout.

## Changement

- `B2LPlatformExpert.cpp` : `product-id` est maintenant coupé par défaut. Le boot-arg `b2l_product_id=1` le remet.
  - Journal : `B2L AUDIO: /product product-id off (default since 0276; b2l_product_id=1 sets it)`.
- Le reste de 0275 est gardé : Bluetooth `b2l_amfi_constraint.h` (0x63), set Metal v133, plug-in audio v2, `acoustic-id` 8017, D94AP.
- Rien sur USERDATA.

## Installé

- SYSTEM-OK, BOOT `4d385cadbdee123c39a3b73b1776266b198cb1b498e2065defce1aff3de02749`, BOOT-OK. Noyau non strippé : `out/mach-0276.unstripped`.
- Téléphone redémarré vers le système.

## Suite

- Lire l'os_log de backboardd du démarrage 0275 (USERDATA en lecture seule) pour savoir pourquoi il s'arrête avec un D94 reconnu.
- Puis trouver une autre voie pour l'audio : `product-id` montré seulement à audiomxd, ou correction du point qui casse backboardd.
