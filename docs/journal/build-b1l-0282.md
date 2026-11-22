# B1L-0282 — Bluetooth : canal audio Skywalk pour bluetoothd

## Pourquoi (session Bluetooth)

- En 0281, la liste Bluetooth marche.
- À la connexion des écouteurs, le noyau refuse le canal audio Skywalk à bluetoothd (`__nexus_register` → EPERM), puis bluetoothd plante sur `os_channel_get_fd(NULL)`.
- Lu sur `out/live/ios26-userdata-bt-281.img`, en lecture seule. Journal noyau : `out/logs/0281-boot.txt`.

## Changement

- `bsd/kern/b2l_sandbox.c` (noyau seulement) : `b2l_sandbox_priv_grant` accorde `PRIV_SKYWALK_REGISTER_USER_PIPE` (12000) au seul bluetoothd (pid `b2l_btd_dyld_pid`). Les autres privilèges ne changent pas.
- Ancienne version : `~/Developer/s10-port/bak-0276-bt/b2l_sandbox.c.0281`.
- Audio : pas de changement depuis 0279. En 0281, l'anneau est prêt (8160 images), mais rien n'a été joué pendant ce démarrage.
- Rien sur USERDATA.

## À vérifier

- « B2L Sandbox: Skywalk user pipe granted to bluetoothd ».
- bluetoothd ne plante plus à la connexion des écouteurs.
- Le son marche toujours au haut-parleur.

## Installé

- SYSTEM-OK, BOOT `5430ce61028e020d3de2ec0adf0112d5f13072efd452f44079753c56c9d525d0`, BOOT-OK. Noyau non strippé : `out/mach-0282.unstripped`.
- Téléphone redémarré vers le système. Journal à tirer : `out/logs/0282-boot.txt`.
