# B1L-0337 — Fond d'écran d'Apple au lieu du fond forcé

## Pourquoi

Piste du propriétaire : le verre trop blanc vient peut-être de la façon dont on force le fond d'écran. `b2l-springboard`
pose une image par-dessus la fenêtre du fond d'écran, et SpringBoard ne reçoit jamais l'état du poster (couleurs,
luminosité, lisibilité). PosterBoard et MercuryPosterExtension n'avaient pas de GPU depuis 0292 (pilote Metal → `nil`).
Sans GPU, leur poster restait noir (B1L-0027).

## Changement

- `userspace/b2l-springboard.m` (sauvegarde `.before-0337`) : `B2L_FORCE_WALLPAPER 0`, aucune image posée. Le correctif
  du Centre de contrôle (arm64e) reste. Payload `b2l_springboard_payload.h` regénéré.
- `userspace/metal/b2l_metal_plugin.m` (sauvegarde `.before-0337`) : PosterBoard et MercuryPosterExtension reçoivent le
  Mali. SpringBoard, chronod, iconservicesagent, WidgetRenderer_Default, SBRendererService et WebKit.GPU restent sans GPU.
- Bibliothèque renommée `b2l-springboard-v6.dylib` (`b2l_exec_graphics_diag.h`, `b2l_userspace_seed.h`, `b2l_amfi_constraint.h`, script) : le noyau ne remplace jamais un fichier déjà installé, d'où l'échec de 0336.
- Set v163.

## À vérifier

- le fond d'écran d'Apple s'affiche-t-il (écran d'accueil, écran verrouillé) ou reste-t-il noir ?
- PosterBoard / MercuryPosterExtension dans `b2l-plugin: … asks for a device` ; plantages (namespace) ;
- le verre : correct sur ce fond ?

## Retour en arrière

`B2L_FORCE_WALLPAPER 1` remet l'image forcée  ; remettre aussi les deux noms dans
la liste `denied`.

## Résultat

BOOT-OK (boot e5ce500f…). **Propriétaire : aucun fond d'écran ne marche, vignettes noires dans « Add New Wallpaper ».** Log : PosterBoard, MercuryPosterExtension et CollectionsPoster reçoivent bien le Mali (profil 1). Mais PosterBoard voit son flou `downsample_4_frag_lph` refusé (texture créée sans `MTLTextureUsageShaderRead`), et CollectionsPoster se voit refuser 31 surfaces R8Unorm sur 32 (IOSurface 'L008', le pilote n'acceptait que 'A008'). CollectionsPoster sort deux fois (namespace 7, LIBXPC). → 0338.
