# B1L-0324 — Nouvel écran du XnuLoader : fond noir et icônes (UEFI, noyau 0323, set v152)

## Pourquoi

Le propriétaire veut un écran d'avant-noyau refait, avec ses icônes (iOS 26, Réglages, redémarrage) sur fond noir.
Le logo de redémarrage, noir sur blanc, devait être inversé en blanc sur noir.

## Changement

- `uefi/XnuLoader/icons/` : les trois images données par le propriétaire (`ios26.png`, `settings.png`,
  `reboot.png`).
- `scripts/make-menu-icons.py` → `uefi/XnuLoader/MenuIcons.h`, pixels 0xAARRGGBB. Pas de PIL sur le Mac : un petit
  lecteur PNG en Python (8 bits, RGB/RGBA/gris/palette).
  - Icônes d'app : recadrées sur ce qui n'est pas blanc, puis découpées en carré arrondi (rayon 22,5 %). Les coins
    blancs des images disparaissent.
  - Redémarrage : inversé (blanc, l'opacité tirée du noir).
- `BootMenuDraw.c` :
  - fond **noir**, cartes `#1c1c1e` ;
  - grande icône iOS 26 (240 px) en tête ;
  - une icône de 128 px à gauche de chaque carte : iOS 26 → « Boot iOS », pause blanche dessinée → « Stay here »,
    Réglages → « Reboot to recovery », flèche blanche → « Reboot ».
- `XnuLoader.inf` : `MenuIcons.h` ajouté.
- `XnuLoader.efi` reconstruit avec `scripts/build-efi.sh` (WORKSPACE = mu-work-b1l-touch) : 76 Ko → 504 Ko.
  Ancienne version et ancien FD gardés dans `~/Developer/s10-port/saved-builds/fd-before-0324/`.
- Le XnuLoader est **intégré au firmware** (FD) : le premier paquetage a refusé un XnuLoader qui n'y était pas
  (« not embedded in this FD »). Le nouvel `.efi` a été copié dans
  `mu-work-b1l-touch/Platforms/Samsung/beyond1ltePkg/B2LXnuEmbedded/`, puis le FD reconstruit avec la dernière étape
  de `scripts/build-boot-firmware.sh` (mêmes `-D FD_BASE=0xC9000000 FD_SIZE=0x400000 …`, `B2L_BOARD=beyond1lte`).
  `prepare-boot-firmware.py` n'a pas été relancé : il était déjà appliqué à cet espace de travail. FD rempli à 27 %.

Test : `tests/boot_menu_draw_test.c` → ok (avec `-Wall -Werror`). Aperçu rendu sur le Mac et envoyé au propriétaire.

## À vérifier

L'écran au démarrage : icônes nettes, coins propres, menu utilisable.

## Résultat

Flashé (BOOT `cf597f6f…`, BOOT-OK). Testé avec 0325 par-dessus.
