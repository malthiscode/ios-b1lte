# B1L-0280 — Bluetooth : réponse locale à 0xfe3b

## Pourquoi (session Bluetooth)

- En 0279, bluetoothd plante environ toutes les 10 s sur l'assertion `bcm_vsc.c:1019`.
- Elle suit notre réponse locale à la commande propriétaire 0xfe3b, qui était trop courte : bluetoothd y lit environ 60 champs.
- C'est aussi la cause des gels de Réglages.
- Lu sur `out/live/ios26-userdata-bt-279.img`, en lecture seule.

## Changement

- `B2LBluetooth.cpp` (noyau seulement) : la réponse locale fait 255 octets, et le refus de 0xfd23 en 8 octets est aussi pris en compte.
- Audio : pas de changement depuis 0279 (lost=0).
- Rien sur USERDATA.

## À vérifier

- « answered here » pour 0xfd23 et 0xfe3b.
- bluetoothd ne redémarre plus en boucle.
- Le son marche toujours.

## Installé

- SYSTEM-OK, BOOT `4bc8d6803c18bb61391cde36b01a4e7694a1c59e565dd47b3e21d7ac096a8699`, BOOT-OK. Noyau non strippé : `out/mach-0280.unstripped`.
- Téléphone redémarré vers le système. Journal à tirer : `out/logs/0280-boot.txt`.
