# B1L-0285 — G1 : le cache de tampons de panfrost garde ses mappages et ses tampons plus longtemps

Plan : `plan-gpu-jesvi.md`, étape G1, accord du propriétaire (« Oui, G1 »). Base : 0284. GPU toujours réservé à backboardd (0269).

## Pourquoi

- Idées reprises de Jesvi-iOS (github.com/jesvi-eulah/Jesvi-iOS, `tools/mesa/panfrost-keepmap.patch` et `panfrost-retain.patch`,
  licence MIT). Le code est réécrit pour notre arbre, pas copié.
- **Mappage gardé** : chaque tampon remis dans le cache de panfrost était démappé, puis remappé à la réutilisation. Chez nous, ce sont
  deux allers-retours IOKit (`hakios_drm_unmap` / `IOConnectUnmapMemory64`). Jesvi les a mesurés à ~12 % du temps du serveur d'affichage.
- **Tampons gardés 30 s au lieu de 2 s** : CoreAnimation crée ses cibles par rafales espacées de quelques secondes. Avec 2 s, le cache
  est presque toujours vide à la rafale suivante, et chaque texture repaie une allocation et un mappage dans le noyau.

## Changement (Mesa, `apply.py` section B1L-0285, sauvegarde `.before-0285` ; fichier touché `pan_bo.c`)

- `panfrost_bo_unreference` ne démappe plus sur Apple ; `panfrost_bo_free` démappe, juste avant de libérer.
- `panfrost_bo_cache_evict_stale_bos` : tampon inutilisé gardé **30 s** ; budget du cache **256 Mo**, les plus anciens partent d'abord.
  Raccourci noté : le budget est un choix entre les 64 Mo de Jesvi et les 343 Mo que backboardd avait marqués purgeables en 0267.
- Ligne `B2L BOCACHE` toutes les 2000 créations de tampon (60 par processus au plus) : réutilisés, neufs, partis par l'âge,
  partis par le budget, Mo en cache.
- Set v133 → v134 (`b2l-metal-v134` **et** `B2LMetalv134`).

## Risques

- **Reprise mémoire du noyau (0258)** : un tampon en cache reste mappé. La reprise sous pression mémoire (jamais vue déclenchée jusqu'ici)
  le compte comme repris, mais ses pages restent tenues par le mappage jusqu'à ce que Mesa le libère (`retained = 0` →
  `panfrost_bo_free` → démappage). Le budget de 256 Mo borne cet écart.
- Mémoire : jusqu'à 256 Mo de plus gardés par processus GPU (seul backboardd aujourd'hui).

## Mesures attendues

- `B2L BOCACHE` : part des tampons réutilisés.
- `B2L HOLD` de backboardd : temps de `-commit` et de `newTextureWithDescriptor:`, à comparer avec 0277–0282.

## Installé

- SYSTEM-OK, BOOT `c8dc77f4fd5d0680cffe6d509e48f8857de5f44c10b54ba112d24d322d49fe5d`, BOOT-OK ; `libhakiosair.dylib` v134 contient
  « B2L BOCACHE », plug-in `B2LMetalv134`. À vérifier au démarrage : `MetalPluginName B2LMetalv134` dans le log.

## Résultat (log `out/logs/0285-boot.txt`)

- Propriétaire : « ça clignote, ça fait des sortes de rollback et ça peut même planter ».
- `MetalPluginName B2LMetalv134` : le bon pilote est chargé, seul backboardd a le GPU.
- **Mémoire épuisée** : mémoire libre à 27 Mo, plus de 130 allocations GPU refusées par le noyau (« no memory for a buffer »,
  B1L-0119), puis **backboardd plante** (pid 63, signal 11, relancé en pid 372) : c'est le clignotement et les retours en arrière.
  backboardd tenait 496 Mo de mémoire GPU, dont 176 Mo en réserve inutilisée ; la reprise du noyau (0258) n'a rendu que 0 Mo.
  Aucune de ces erreurs en 0281 et 0282.
- Le cache marche : 24 258 tampons réutilisés pour 1 742 neufs (93 %). Aucun tampon n'est parti par l'âge, et 707 sont partis
  à cause du budget.
- Conclusion : garder 30 s et 256 Mo est trop pour la mémoire du téléphone. Retour à 2 s en B1L-0286 ; le mappage gardé reste.
