# B1L-0345 — Cibles d'écran en portrait quand l'interface est en paysage (Geometry Dash)

**But.** Afficher le jeu ouvert depuis l'icône (0344 : icône qui clignote, deux images périmées qui alternent).

**Cause.** `CA::OGL::MetalContext::get_surface(Surface*, bool)` (QuartzCore 23G90) choisit une cible tournée si :
- il y a un écran et la compression du framebuffer est coupée,
- le bit 2 de l'octet +0x108 de la surface est à zéro,
- et l'orientation de l'écran (octet bas de +0x2a0) vaut 1 ou 3.

Il demande alors une texture 3040x1440 avec `rotation` 3 sur la surface 1440x3040. Le GPU d'Apple tourne ; notre pilote ignore `rotation` et refuse sur la taille.

**Changement (`userspace/metal/scion/hakiosmetal_air.m`, set v165).**
- Dans backboardd seulement, au chargement du pilote : le `tbnz w8,#2` de `0x183efdf1c` devient un `nop`. `w26` reste à 0 : c'est le chemin que QuartzCore prend avec la compression de framebuffer.
- `update_projection_matrix()` teste le même bit (0x400 de +0x134) : sans lui, la composition se fait en coordonnées portrait dans la texture portrait.
- Le mot est comparé avant d'être écrit. Journal : `B2L ROTATION: …`. Mécanisme repris de `userspace/b2l_accel_gles.c`.
- Limite : 23G90 seulement. Le chemin CAMetalLayer des apps (`allocate_drawable_texture`) peut aussi poser `rotation` ; il n'est pas traité.

**Vérifié avant flash.** Compilation ; contrôle du mot, de la cible du saut et de la suite contre le QuartzCore extrait du cache.

**Non vérifié.** Que la page se corrige sur ce noyau dans backboardd en mode Metal (le même mécanisme marchait en mode GLES), le sens de l'image en paysage droit et gauche, le retour au portrait.

**Flash.** BOOT `d684db1b…` relu (BOOT-OK), SYSTEM-OK.

**Résultat.** Panique du noyau au démarrage, au premier `sys_icache_invalidate` du correctif (far `0x18e099f1c` = `0x183efdf1c` + glissement) : bug du gestionnaire de cache du noyau, pas du correctif de rotation (build-b1l-0346.md). La rotation elle-même n'a donc pas été testée.
