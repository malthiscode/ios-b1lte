# B1L-0281 — Bluetooth : retour au comportement 0279 pour 0xfd23

## Pourquoi (session Bluetooth)

- Régression 0280 : le noyau répondait lui-même au refus de 0xfd23 (lecture des infos de contrôleur en cache).
- Le lecteur de bluetoothd exige une longueur exacte (assertion `bcm_vsc.c:939`), donc bluetoothd plantait dès l'allumage.
- Lu sur `out/live/ios26-userdata-bt-280.img`, en lecture seule.

## Changement

- `B2LBluetooth.cpp` (noyau seulement) : même comportement qu'en 0279, sauf 0xfe3b, qui garde sa réponse de 255 octets.
- Audio : pas de changement depuis 0279.
- Rien sur USERDATA.

## Installé

- SYSTEM-OK, BOOT `0ec0472d30e6c99cbc04b09738e5bafdad68d6e5f4642a5c56583c0ade678916`, BOOT-OK. Noyau non strippé : `out/mach-0281.unstripped`.
- Téléphone redémarré vers le système. Journal à tirer : `out/logs/0281-boot.txt`.
