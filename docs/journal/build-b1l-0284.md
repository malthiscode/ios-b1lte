# B1L-0284 — réactiver fairplayd (Météo)

Base : 0283. Accord du propriétaire pour écrire sur USERDATA : « Oui reactive fairplayd ».

## Pourquoi
L'os_log du 0283 montre que les requêtes WeatherKit reçoivent un 401. AMS ne peut pas les signer avec Mescal, parce que launchd ne trouve pas `com.apple.fairplayd.H2`. Ce service est désactivé dans `disabled.plist` depuis le USERDATA propre du 0250 (`scripts/prepare-ios-root.py`).

## Changement
`bsd/kern/b2l_bt_seed.h` : `b2l_bt_launchd()` prend maintenant le nom de la clé en paramètre. Le même renommage sur place (même longueur) sert pour `com.apple.fairplayd.H2`, qui devient `com.apple.fairplayd.HX`. Ce renommage est la seule écriture sur USERDATA.

Boot-arg `b2l_fp_launchd` :
- 1 (défaut) : réactive fairplayd ;
- 2 : remet la clé, fairplayd est de nouveau désactivé ;
- 0 : ne touche pas le fichier.

## À vérifier
- Journal noyau : `B2L FAIRPLAY SEED: launchd override of fairplayd.H2: renamed, fairplayd enabled (1)`.
- fairplayd tourne-t-il, ou plante-t-il faute de matériel FairPlay ? Chercher des rapports `fairplayd*.ips`.
- Météo : la recherche de ville marche-t-elle ? Les données s'affichent-elles ?
- Le son, le Bluetooth, le Wi‑Fi et l'écran doivent marcher comme en 0283.

## Build
- BOOT abf41c2a… (relu et vérifié). SYSTEM OK. Le noyau contient la chaîne `B2L FAIRPLAY SEED`. `out/mach-0284.unstripped` est gardé. Téléphone redémarré vers iOS.
