# B1L-0274 — Identité matérielle D94AP (son d'iOS et bluetoothd)

## Pourquoi

- En 0273, `acoustic-id=8017` est bien posé, mais audiomxd dit encore « ProductID is not defined in PlatformUtilities_iOS::GetProductType(), and AcousticID '8017' is not handled » : pas de table de routage, la lecture ne démarre jamais (aucun `start_io`).
- Le type de produit de MobileGestalt (MGGetProductType, HWModelStr) vient de `hw.model`. Sur arm, `hw.model` = `getModelName()` = le premier « compatible » de la racine de l'arbre :
  - chez nous `samsung,beyond2lte` (XnuLoader/DeviceTree.c), d'où MGGetProductType = 0xffffffff ;
  - sur un vrai iPhone17,2 : `D94AP`.
- C'est aussi la cause du plantage de bluetoothd (session Bluetooth).
- ios-pipa met `D22AP`, `iPhone10,3` en tête de sa racine.

## Changements (accord du propriétaire : « Oui, autorise »)

- `B2LPlatformExpert.cpp` :
  - le compatible de la racine devient `D94AP`, `iPhone17,2`, puis `samsung,beyond2lte` et la suite, gardés pour nos pilotes ;
  - `target-sub-type` = `D94AP` ;
  - journal : `B2L: root compatible now D94AP, iPhone17,2, samsung,beyond2lte; hw.model D94AP` ;
  - coupure : `b2l_hw_model=0`.
- `b2l_profile_cache.h` v6 : le cache MobileGestalt est renommé une fois en `com.apple.MobileGestalt.b2l-before-profile-v6.plist` (rien n'est effacé), et iOS le recalcule.
- Bluetooth v3 de la session Bluetooth : la dylib trouve bluetoothd par `dladdr()`. Coupure : `b2l_bt_platform=0`.
- Le reste ne change pas : `/product/audio acoustic-id=8017` (0273), plug-in v2 avec trace, set Metal v133.

## Risques

- Tout iOS voit un vrai D94 (iPhone 16 Pro Max) : interface, caméra, capteurs.
- bluetoothd pourrait choisir la plateforme Bluetooth du vrai D94 plutôt que 0x16 (réserve de la session Bluetooth).

## Installé

- SYSTEM-OK, BOOT `cdc4ae47ee139e7a3751f93a18e5b05f27104a9b851c9b245b2c464659f8cdf0`, BOOT-OK. Noyau non strippé : `out/mach-0274.unstripped`.
- Téléphone redémarré vers le système.
