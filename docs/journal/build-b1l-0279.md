# B1L-0279 — audio sans saccades au début

## Pourquoi

- 0278 : le son marche, mais « peut-être saccadé au début ».
- Journal 0278, première lecture après le démarrage : `in=107520 lost=37220 resyncs=22` en 2,24 s. Les lectures suivantes ne perdent presque rien.
- Cause 1 : pendant la fin du démarrage, les cycles du HAL (1024 images, 21 ms) arrivent en retard, puis il rattrape en rafale.
  - L'anneau de 4080 images avec 1024 images d'avance ne tient qu'environ 21 ms de retard.
  - Au-delà, le lecteur dépasse le rédacteur (trou, recalage), puis la rafale déborde (`lost`).
- Cause 2, un bogue : si le pas du service (10 ms) a plus de 5 ms de retard pendant une rafale, la remise à zéro efface des images que le rédacteur vient de poser. Ces trous n'étaient pas comptés.

## Changement (`B2LAudio.h`, noyau seulement)

- `B2L_AUD_RING_BYTES` : 65280 → 130560, soit 8160 images (170 ms, 32 périodes de 4080 octets).
- `B2L_AUD_LEAD` : 1024 → 2048 images (43 ms d'avance après un recalage).
- `b2l_aud_svc_step` : quand le rédacteur est devant le RDMA, la remise à zéro s'arrête avant ses images.
- Test hôte `audio/tests/test4.cpp` (ASan/UBSan) : OK.
  - Nouveau cas : un pas du service en retard de 31 ms pendant une rafale.
  - Sans la protection, le test échoue (vérifié).
- Ancienne version : `~/Developer/s10-port/audio-next-parked/B2LAudio.h.before-0279`.
- Rien sur USERDATA ; le plug-in ne change pas.

## Risque

- La latence de sortie passe de 21 à 43 ms environ. C'est sans effet sur les sons système.

## À vérifier

- `ios-ready … (8160 frames)`.
- Sur la première lecture : `lost` et `resyncs` faibles dans `ios-stop`, et le son régulier dès le début.

## Bluetooth (même build)

- `B2LBluetooth.cpp` (session Bluetooth) : le décodeur H4 ajoutait deux fois la longueur des paquets de 8 octets ou moins, donc le refus de 0xfd23 n'était jamais reconnu ; c'est corrigé. Journal : « vendor HCI 0xfd23 unknown to the chip (...): answered here ». Coupure : `b2l_bt_vsc=0`.

## Installé

- SYSTEM-OK, BOOT `e01aed84c3e13bb5feed50be7c5cc5fa15b5ab1ccfabd1b7cc35bb862cc6e9fd`, BOOT-OK. Noyau non strippé : `out/mach-0279.unstripped`.
- Téléphone redémarré vers le système. Journal à tirer : `out/logs/0279-boot.txt`.

## Résultat

- Propriétaire : « l'audio marche mieux ».
- Journal : anneau de 8160 images. 9 démarrages d'IO, `in=2806784 lost=0 resyncs=9`, aucune `ios-fault`.
