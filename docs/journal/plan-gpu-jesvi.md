# Plan GPU d'après Jesvi-iOS (2026-11-21)

Point de départ : B1L-0269. Le GPU est réservé à backboardd, et les icônes, widgets et Centre de contrôle sont dessinés sans GPU,
par le code d'Apple. Le propriétaire confirme que « le pilote marche comme avant ».

Source : github.com/jesvi-eulah/Jesvi-iOS (iOS 16.5 sur S20, Exynos 990, Mali-G77). Lu sur GitHub ; copies de travail des fichiers
dans le scratchpad. Licences : noyau en APSL 2.0 ; outils, scripts, docs et userland en **MIT** (fichier `LICENSE`). Leurs correctifs
Mesa (`tools/mesa/`) peuvent donc être repris en citant la source.

## Ce que Jesvi fait, et ce que ça nous apprend

1. **Même chemin que nous** : traduction AIR → NIR (`tools/mesa/air/`, même lignée HakiOS), puis panfrost sur le Mali, puis leur noyau.
   Leur interface reste sur iOS 16 (CoreAnimation en OpenGL ES), donc leurs leçons de pilote s'appliquent, pas leur chemin d'affichage.
2. **Metal coupé par défaut dans tout le système, ouvert app par app** : une liste de processus vérifiés (fond d'écran Photos, Plans,
   Google Maps, Localiser). Ils excluent nommément **PosterBoard**, dont les titres sortaient en « glyphes brouillés » avec leur pilote :
   le même genre de casse que nos icônes. C'est la stratégie de notre 0269, en plus fin.
3. **Correctifs panfrost de performance** (`tools/mesa/`, MIT), avec leurs mesures :
   - `panfrost-keepmap.patch` : un tampon remis en cache garde son mappage CPU. Le mappage et le démappage à chaque réutilisation
     coûtaient **~12 % du temps du serveur d'affichage**.
   - `panfrost-retain.patch` : le cache de tampons garde un tampon inutilisé **30 s** (budget 64 Mo) au lieu de 2 s. CoreAnimation
     crée ses cibles par rafales espacées de 5 à 25 s, donc avec 2 s le cache était toujours vide.
   - `panfrost-lazy-heap.patch` : le tas du tiler est créé au premier dessin, avec 32 Mo au lieu de 128 Mo. Leur noyau le remplit
     entièrement dès sa création, comme le nôtre : nos logs montrent **128 Mo par processus**, même pour ceux qui ne dessinent presque rien.
   - `panfrost-disk-cache-darwin.patch` : cache disque des shaders sur Mach-O. **Nous l'avons déjà** (B1L-0095, même idée par LC_UUID).
4. **Correctifs de compatibilité pour les apps** (`docs/devices/x1slte/wallpaper-metal.md`, `maps-rendering.md`) :
   - texture ou échantillonneur vide → valeur par défaut (CoreImage en dépend) ;
   - `air.simdgroup.barrier` ;
   - `threadExecutionWidth` = 32 ;
   - mémoire de threadgroup donnée au dispatch ;
   - plans 420f/420v en R8/RG8, codes CoreVideo (R16F, RG16F…) ;
   - `air.pack.unorm4x8` ;
   - écriture retour des IOSurfaces copiées ;
   - formats RGB10A2 et RG11B10F, couleur de mélange.
   Leur validation : comparaison pixel par pixel avec macOS (7 filtres CoreImage identiques à 1–2 près sur le flou).

## Nos mesures actuelles (logs 0277 à 0282, seul backboardd sur le GPU)

- Compilation panfrost par démarrage : **7 à 14 s**, 177 à 214 compilations, au plus 1,5 à 3,9 s pour une seule. Au premier
  démarrage d'un nouveau Mesa (0269) : 59,5 s, dont une de 49 s. Le cache disque marche donc en partie ; on ne sait pas encore
  ce qui manque.
- `-commit` de backboardd : 40 s cumulées sur un démarrage (0269).

## Plan (un build par étape, mesuré, documenté dans `build-b1l-NNNN.md`)

| # | Étape | Pourquoi | Mesure | Coût |
|---|---|---|---|---|
| G1 | **Cache de tampons : garder le mappage et garder les tampons 30 s** (idées de keepmap et retain, adaptées à notre `pan_bo.c`, qui a déjà son propre démappage) | ~12 % du temps du serveur d'affichage chez eux ; c'est notre processus le plus chargé | temps de `-commit` dans `B2L HOLD`, fluidité ressentie | 1 build Mesa |
| G2 | **Cache de shaders : compter les réussites et les échecs**, puis corriger ce qui rate encore | 7 à 14 s de compilation à chaque démarrage, alors qu'un cache plein devrait viser près de 0 | réussites/échecs par démarrage, temps de compilation | 1 build de mesure + 1 de correctif |
| G3 | **Tas du tiler créé au premier dessin** (idée de lazy-heap) | 128 Mo par processus GPU ; indispensable avant de rouvrir le GPU à d'autres apps | `mali mem` par processus | 1 build Mesa |
| G4 | **Liste de processus GPU par boot-arg** (comme leur liste), puis ouverture app par app : Dictaphone (Test Metal) d'abord, puis Plans, Photos… Toujours sans SpringBoard, iconservicesagent, chronod et PosterBoard. | garder l'interface juste tout en accélérant les apps qui en profitent | Test Metal, photo de chaque app | 1 build, puis 1 essai par app |
| G5 | **Compatibilité apps** : passer chaque point de leur liste dans l'app Test Metal, et ne corriger que ce qui échoue chez nous | CoreImage, Photos, Plans ont besoin de ces chemins | tests Test Metal | 1 build par lot vérifié |
| G6 | Plus tard : les icônes de nouveau sur le GPU, seulement après la preuve et la correction des copies de textures (plan précédent, P4/P5) | c'est la seule partie de l'interface encore cassée sur le GPU | photos en mouvement | à chiffrer |

Ordre recommandé : **G1 puis G2** (fluidité tout de suite, sans risque pour l'affichage), puis G3 et G4 ensemble pour rouvrir les apps.

## Ce que je ne peux pas vérifier d'ici

- Leurs chiffres (12 %, rafales de 5 à 25 s) viennent du Mali-G77 sous iOS 16 ; il faut les remesurer chez nous à chaque étape.
- Le ressenti de fluidité : vos retours restent la mesure finale.
