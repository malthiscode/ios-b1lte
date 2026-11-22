# B1L-0349 — Mesa survit à un tampon refusé (Mesa, set v166)

**Symptôme (`out/logs/0346b-boot.txt`).** backboardd meurt en SIGSEGV à l'adresse 0x148 juste après cinq refus, l'interface redémarre.

**Cause.** `panfrost_ptr_map + 772` : `pan_alloc_staging` rend NULL faute de mémoire, et l'`assert(staging)` est retiré à la compilation.

**Changement.** `userspace/mesa/air-b2l/apply.py` (sauvegarde `.before-0349`) : envoi des lots en attente puis second essai, sinon l'accès échoue proprement et libère ses références. Même protection pour un mmap refusé et pour `pan_resource_modifier_convert`.

**Limite.** En cas de second refus, l'écriture de la texture est perdue (image fausse au lieu d'un plantage), et `rwa_texture_write` répond succès.

**Vérifié.** Patch rejoué deux fois à l'identique sur sources fraîches ; test hôte des références ; `cbz x0` avant `ldr w8,[x0,#0x148]` dans la bibliothèque construite.

**Flash.** BOOT `21ff7dd9…` (BOOT-OK), SYSTEM-OK, un seul redémarrage après 0347–0349. Le téléphone a quitté TWRP ; démarrage d'iOS non observé.

**Résultat.** Testé. backboardd n'a pas planté. Le chemin de secours n'a pas servi (0 message `B1L-0349`), donc il reste non exercé sur le téléphone.
