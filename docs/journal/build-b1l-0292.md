# B1L-0292 — O1 : GPU pour tous les processus sauf l'écran d'accueil (set v139)

## Changement (un seul)

`userspace/metal/b2l_metal_plugin.m` : le Mali est donné à **tout processus**, sauf ceux qui dessinent les icônes,
widgets, instantanés et fonds d'écran de l'accueil, dont le dessin GPU est encore faux : SpringBoard, chronod,
iconservicesagent, WidgetRenderer_Default, SBRendererService, PosterBoard, MercuryPosterExtension.

Boot-args : `b2l_gpu_icons=1` rend le GPU aux trois premiers (comme avant), `b2l_gpu_all=1` à tous,
`b2l_gpu_apps=0` revient à 0269 (backboardd seul). Logique testée sur le Mac (scratchpad `gate.c`, 10 cas).
Set v138 → v139, build des bibliothèques (sans Mesa).

## Pourquoi maintenant

En 0248, ouvrir à tous avait ralenti surtout par les compilations (cameracaptured : 32 s). Depuis 0290 les shaders
sont gardés sur disque pour tous les processus, donc chaque app compile une fois, pas à chaque démarrage. La mémoire
rendue plus vite (0291) laisse de la marge.

## À mesurer

Processus qui reçoivent le GPU (`b2l-plugin: ... asks for a device`), `mali mem` par processus, mémoire libre,
« no memory », plantages, shaders compilés (premier démarrage : toutes les apps compilent une fois), i/s (`B2L PROF`).
Le propriétaire : ouvrir quelques apps (Photos, Plans, Réglages, Météo) et dire si c'est plus fluide, et si quelque
chose s'affiche mal.

## Résultat

Propriétaire : ni accélération ni ralentissement ressentis. Log : seuls **backboardd et cameracaptured** demandent le
GPU (`b2l-plugin: ... asks for a device`). Les apps lancées (Calculette, Réglages, Téléphone…) ne créent jamais de
périphérique Metal : sur iOS, l'écran entier (apps, animations d'ouverture, Liquid Glass) est composé par backboardd,
qui avait déjà le GPU. O1 ne change donc presque rien ; il reste en place (Photos, Plans, Appareil photo, jeux
profiteront du GPU s'ils le demandent). i/s : 2 à 15,6 sur 10 s. 0 plantage.

**Le frein trouvé** (profileur `B2L PROF`) : le fil de rendu de backboardd (`com.apple.coreanimation.display`) tourne
60 à 78 % du temps, dont **~75 % dans le noyau**, sur des défauts de page en écriture (`esr 0x92000047`, ~30 000 par
seconde en tout). Adresses de retour rapportées à libhakiosair (base `0x116700000`, trouvée par les sites d'appel) :
`panfrost_bo_create+312` → `bzero` (mise à zéro B1L-0185 d'un tampon repris du cache), `panfrost_emit_const_buf+2456` →
`memcpy` (constantes des shaders), `util_copy_box` (textures). Mesa démappe un tampon en le rendant au cache et le
remappe à la réutilisation : chaque page refait faute. Fausse piste écartée : la console du noyau n'écrit qu'en RAM
après l'interface (`b2l_early_putc`), pas sur un port série.
