# Point sur l'accélération Metal, comparée à HakiOS (2026-11-22)

Source : `oosexgang/hakios-xnu-kernel`, branche `common`, `Documentation/metal-glass.md` et l'index `Documentation/`
(lus le 2026-11-22). **Le fichier LICENSE du dépôt répond 404 : licence inconnue. On ne copie donc aucun code, on ne reprend que des idées.**
Leur cible : Adreno 642 (Qualcomm), iOS 27.2 ; la nôtre : Mali-G76 (Exynos 9820), iOS 26 (23G90).

## Où on en est (après 0340, set v164)

| Point | Nous | HakiOS |
| --- | --- | --- |
| Rendu de l'interface | backboardd sur le Mali, 16–37 i/s (0333) | backboardd sur l'Adreno, 60 i/s écran verrouillé, 66–70 i/s balayage accueil, 80 i/s Réglages |
| Familles GPU annoncées | profil 1 : Apple1 + Apple2 | `metal_profile=1` : Apple2 (Apple4 et tile shading non annoncés) |
| Liquid Glass | **correct sur fond sombre** (0339) ; trop clair sur fond clair | dessiné (horloge, dock, recherche, alertes) |
| Fond d'écran Apple (PosterBoard) | noir : `VTMTSComputeFunction1x1` refusé (mémoire de groupe) | poster « Stripe Yellow » dessiné après `components/wallpaper/inject_poster.c` |
| Mémoire de tuile / memoryless | essayée (0329), coupée (0334) : coins jaunes | encore ouverte chez eux aussi |
| Cache de shaders | sur disque, un seul fichier (0290) | cache d'étages + cache de traduction `air_to_nir_kept` |
| Safari / WebKit.GPU | sans GPU depuis 0294 | — |

## Ce qu'on peut reprendre (idées, pas de code)

1. **Fonds d'écran d'Apple.** Leur poster ne s'est affiché qu'après deux choses : répondre aux chiens de garde de
   PosterBoard, et multiplier les délais d'instantané (`-[PBFPosterSnapshotThrottlePolicy captureTimeoutInterval]`,
   `-[PUIPosterSnapshotRequest timeoutInterval]`). Chez nous s'ajoute la mémoire de groupe de threads de
   `VTMTSComputeFunction1x1`. Eux placent les bases de mémoire de groupe à chaque dispatch (`rwa_threadgroup_memory_at`) ;
   nous refusons le dispatch quand la taille n'est pas donnée par `-setThreadgroupMemoryLength:atIndex:`. → prochain build Metal.
2. **Chemins de CoreAnimation sans tile shading.** Ils confirment notre choix : sans Apple4, CA prend ses chemins « render
   pipeline », pas les flous en tuiles (`tile_simd_blur_*`, `backdrop_capture_compute_tile_*`).
3. **Écriture en lecture/écriture.** Leur liste de bogues contient « read_write texture bound without read access » :
   c'est le même cas que notre 0338 (PosterBoard, `ShaderRead`).
4. **Masques vectoriels.** `path_rect`/`path_cubic` écrivent une couverture en R32Float puis `path_blit` la relit
   par framebuffer fetch (`[[color(2)]]`). Ils l'émulent par une copie (texel fetch) : à vérifier chez nous si des
   formes vectorielles sortent noires.
5. **Paramètres de démarrage.** Leurs boot-args GPU : `metal=1`, `metal_profile=N`, `metal_sync=1`, `fb_hz=120`,
   `adreno_speed=N` (index de la table de fréquences), `swaplog=1`, `sample_every=N`. Un `Documentation/boot-args.md`
   est généré à partir du code. Pour nous : profil Metal, fréquence GPU, journal des présentations et `b2l_app_gd`
   pourraient passer dans le menu de démarrage du XnuLoader.
6. **Thermique.** Ils plafonnent l'OPP GPU par un limiteur (`thermal.md`) ; nous avons les seuils Samsung (0316/0319).

## Ce qui reste ouvert chez nous

- fonds d'écran d'Apple (point 1) ;
- verre trop clair sur fond clair (à comparer à un vrai iPhone : un fond clair donne un verre clair, mais pas opaque) ;
- clignotements et cadence (0335 à mesurer) ;
- Safari sur le GPU, décodage vidéo matériel ;
- Geometry Dash (0340 en test).
