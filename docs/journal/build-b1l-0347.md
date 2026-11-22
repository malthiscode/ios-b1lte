# B1L-0347 — Plage partagée du noyau agrandie (noyau, set v165)

**Symptôme (`out/logs/0346b-boot.txt`).** 1229 tampons GPU refusés, parfois avec plus de 1 Go de RAM libre, puis backboardd tué (→ 0349). Au même moment que les premiers refus, une vague de démons meurt en SIGBUS.

**Cause.**
- Lu dans les sources et le noyau compilé : dans la plage partagée, toute allocation jusqu'à 128 Mo passe par les objets de garde de XNU. Ils réservent 2 à 16 cases arrondies à une puissance de deux : 256 Ko prend un bloc de 2 Mo, 28 Mo un bloc de 128 Mo, 40 Mo un bloc de 256 Mo.
- Déduit, non mesuré : la plage de 2609 Mo était pleine. C'est la seule sortie en échec plausible du chemin des petits tampons quand la RAM est libre.
- Hypothèse : les pages lues depuis le disque passent en partie par cette plage, ce qui expliquerait les SIGBUS.

**Changement.** `xnu/osfmk/kern/zalloc.c`, `patches/0112-b2l-zone-map-va.patch` : réservation d'adresses des zones 24 → 16 Go (toujours deux fois `ZONE_MAP_MAX`). `kmem_range_startup` répartit les 8 Go : partagée ≈ 2,6 → 6,2 Go, privée ≈ 1,3 → 3,1 Go, pointeurs ≈ 2 → 4,7 Go (valeurs calculées). Les objets de garde restent actifs et inchangés.

**Vérifié.** Patch réversible ; calcul qui retrouve les plages de 0346 ; `0x400000000` dans `zone_set_map_sizes` du noyau construit.

**Risques.** Agencement mémoire jamais démarré avant ce build. 8 Go d'adresses en moins pour les zones. Retour arrière : `zalloc.c.before-0347`.

**À vérifier.** Démarrage ; `kmem_data_shared_range` ≈ 6,2 Go dans le journal ; « no memory for a buffer » proche de 0 ; pas de vague SIGBUS ni de lignes colorées. Attention : « GPU buffers alive » compte les tas de tiler pour 128 Mo au lieu des 16 Mo adossés.

**Flash.** BOOT `0ba5d651…` (BOOT-OK), SYSTEM-OK.

**Résultat.** Testé (propriétaire : « ca marche bien »). Plage partagée 6 Go, zones 16 Go. 0 refus de tampon (1229 avant), 0 refus d'IOSurface, 0 mort en SIGBUS, un seul backboardd. La cause déduite est donc confirmée par l'effet.
