# Suivi du plan GPU

Mis à jour à chaque build GPU. Détail de chaque build : `build-b1l-NNNN.md`.

## But (propriétaire, 2026-11-21)

**Accélération GPU généralisée à tout le système**, pas seulement l'écran d'accueil. Gestion de la mémoire et fluidité,
**60 i/s stables** sur l'accueil, à l'ouverture des apps et dans les apps. Liquid Glass juste et rapide.

## Où on en est

| | État |
|---|---|
| Pilote en service | set v168 et noyau 0353 (miniatures de Photos, flashé, non testé, dernier build demandé) ; GL des apps : b2l-gl-v14 (0352, testée) ; précédent testé : 0352 (set v167, GL v14) |
| Qui a le GPU | **tous les processus**, sauf SpringBoard, chronod, iconservicesagent, WidgetRenderer, SBRendererService, PosterBoard et son extension (icônes encore fausses sur le GPU) et WebKit.GPU (0294, pages blanches). En pratique : backboardd, cameracaptured, Spotlight. |
| Charge du GPU | 3 à 12 % : **le GPU n'est pas le frein**. Le frein : le fil de rendu de backboardd passe ~75 % de son temps dans le noyau, sur des défauts de page (0292). |
| Images par seconde | 0291 : jusqu'à **59,8 i/s** sur 10 s ; 6 à 23 i/s dans les moments chargés |
| Shaders | lecture du cache 98 ms (6,7 s avant G2) ; reste ~3,5 s de traduction AIR → NIR par démarrage |
| Mémoire GPU de backboardd | 615 Mo (0291), dont 185 Mo dans le cache de tampons de Mesa |

## Étapes

Légende : ✅ fait et vérifié · 🔄 en cours · ⏳ à faire · ❌ essayé puis annulé

| # | Étape | État | Build | Résultat / but |
|---|---|---|---|---|
| — | GPU réservé à backboardd, icônes justes | ✅ | 0269 | confirmé par le propriétaire |
| G1 | Cache de tampons gardé 30 s | ❌ | 0285, 0286 | mémoire épuisée → annulé, retour en 0287 (un gel total à 142 s, cause non trouvée) |
| G2 | Cache de shaders rapide | ✅ | 0288–0290 | lecture 6716 ms → 98 ms (cache Mesa en un seul fichier) |
| M1 | Tampons repris par le noyau traités comme libres | ✅ | 0291 | refus 3412 → 0 ; backboardd 908 → 615 Mo |
| F0 | Compteur d'images par seconde | ✅ | (déjà là) | lignes `B2L PROF ... swaps` ; 0291 : jusqu'à 59,8 i/s sur 10 s, 6 à 23 i/s quand le noyau charge le processeur |
| D1 | Couper le log des chemins chauds (`B2L PASS`, `PROF`…), garder les compteurs | ⏳ | | ~5000 lignes par démarrage écrites pendant le dessin |
| O1 | **GPU ouvert à tous les processus, sauf** SpringBoard, iconservicesagent, chronod, WidgetRenderer, SBRendererService, PosterBoard | ✅ | 0292 | en place, mais peu d'effet : les apps ordinaires ne dessinent pas sur le GPU elles-mêmes, **backboardd compose tout l'écran** |
| C0 | **Fil de rendu de backboardd : défauts de page** → tampons mappés d'un coup (`kIOMapPrefault`) | ✅ | 0293 | défauts 300 000 → ~1 000 par 10 s ; part noyau ~90 → ~62 % ; peu ressenti |
| W1 | Safari : WebKit.GPU sans GPU (pages blanches avec le Mali) | ✅ | 0294 | Safari remarche ; le faire marcher sur le GPU plus tard |
| C2 | Fil de rendu : quels appels système restent | ✅ | 0295, 0296 | `fb 5` (fin d'image) 2 à 5 s par 10 s : **copie de l'image par le processeur une fois sur deux** |
| Z1 | **Affichage direct à chaque image** : mémoire contiguë réservée au démarrage pour les 2 surfaces de backboardd | ✅ | 0297 | plus aucune copie (blit 0) ; un peu plus fluide ; i/s encore 5–22 |
| Z2 | Plus tard : programmer la SysMMU de l'affichage (comme Android) pour afficher toute surface sans copie | ⏳ | | supprime le besoin de mémoire contiguë |
| I1 | Preuve de la casse des icônes sur le GPU (diagnostic ciblé), puis correction | ⏳ | | condition pour ouvrir SpringBoard et les icônes |
| O2 | GPU ouvert à SpringBoard, iconservicesagent, chronod, PosterBoard | ⏳ | | généralisation complète |
| C1 | **Moins d'appels au pilote par image** : ~24 envois GPU, ~150 madvise, ~70 WAIT_BO par image ; créations de tampons à ~1,3 ms | 🔄 | 0299 | 0299 : madvise ÷ 6 à 20, 0 « no memory », i/s inchangés ; **prochain : créations de tampons** (`mali 66`, ~1,5 ms chacune, jusqu'à 2 s par 10 s) |
| LG | Liquid Glass : flou et réfraction justes et rapides | 🔄 | 0298 | 0298 : ~100 dessins par image ; verre + flou = 3 à 11 % seulement, pas le frein principal |
| G3 | Tas du tiler créé au premier dessin | ⏳ | | gain réel faible : 16 Mo par processus inactif |

## Toutes les pistes vers 60 i/s (bilan 0299, 2026-11-21)

Budget : 16,7 ms par image. Aujourd'hui le fil d'affichage de backboardd (`com.apple.coreanimation.display`) met
~65 ms par image, dont ~73 % dans le noyau. Le GPU, lui, travaille ~10 ms par image (busy 13–22 %).

| # | Piste | Preuve | Gain possible | Mesure / build |
|---|---|---|---|---|
| P1 | **Coût de chaque appel au pilote** (aller-retour mach_msg + IOKit) : ~170 appels par image | noyau `mach_msg2` 3 000–9 000 éch./10 s, mais le pilote ne compte que quelques µs par appel | le plus gros : le trou est entre l'app et le pilote | 0301 (`B2L RTT`) |
| P2 | Création de tampons ~1,5 ms (`mali 66`) — **cause trouvée (0301) : purge à chaque création, mémoire libre 30–44 Mo < 256 Mo ; corrigé en 0302** | 0299 : jusqu'à 2 s par 10 s | ~10–30 ms par image dans les pics | 0300 (`B2L CREATE`) ; puis garder les petits tampons plus longtemps ou les sous-allouer |
| P3 | `madvise` **BSD** (appel système 75, pas celui du pilote) : 16 000 éch. | 0299 SVC | inconnu : qui l'appelle ? (libmalloc ? Mesa ?) | à mesurer (adresse de retour) |
| P4 | `workq_kernreturn` (368) : 25 000 éch. | fils GCD réveillés / endormis à chaque tampon de commandes | moyen | à mesurer |
| P5 | WAIT_BO (`mali 65`) 5 800–17 800 par 10 s : Mesa demande au noyau « fini ? » | 0299 | ~70 appels par image en moins si le numéro du dernier travail fini est lu en mémoire partagée | après P1 |
| P6 | Travaux GPU exécutés **un par un et jusqu'au bout** par le noyau (pas de recouvrement sommets/fragments) ; fin d'image (`fb 5`) attend le GPU 0,1–0,9 s par 10 s | `b2l_mali_core.c` (« Jobs are run one at a time ») | moyen : préparer l'image suivante pendant que le GPU dessine | plus tard |
| P7 | ~24 envois GPU par image (un par passe) | 0297/0299 | moins d'envois = moins d'appels | regrouper (C1) |
| P8 | Outils de diagnostic eux-mêmes : échantillonneur toutes les 2 ms, `B2L PERF`, chronomètres, `-v debug=0x14e` | — | à chiffrer : build « tout coupé » pour mesurer le vrai plafond | D1 |
| P9 | Fréquence des cœurs : M4 ~2080 MHz, A75 ~2210, A55 ~1845 après DVFS | log CPUFREQ | faible à moyen (M4 max 2730 MHz sur Exynos 9820, d'après les specs publiques ; à vérifier) | plus tard |
| P11 | **Un tampon de commandes tient le verrou Mesa 30–35 ms** (dessins ~0,3 ms chacun ?) : c'est le vrai plafond (~15–30 i/s) | 0302 `B2L HOLD` | le plus gros | 0303 (`B2L COMMIT`) |
| P12 | **Mémoire GPU de backboardd qui grimpe** (344 → 1126 Mo, 623 Mo en cache Mesa) depuis que la purge ne se déclenche plus (0302) | 0303 `mali mem` | explique « fluide puis lent » | 0305 : cache plafonné à 160 Mo |
| P13 | Un shader met 48,7 s à compiler (registres Bifrost) en tenant le verrou : ~50 s figé au 1er démarrage après un build Mesa | 0303 | démarrage | plus tard |
| P14 | Démappage / remappage des tampons à chaque passage par le cache de Mesa (`pmap_remove`, prefault) | 0304 KSTK | 1er coût noyau après les réveils de fils | ✅ 0306 : ÷ 13, i/s ~12 → ~19,5 |
| P15 | **Disque (UFS) attendu par pas de 1 µs** avec endormissement à chaque pas : chaque lecture de fichier brûle du processeur | 0304 KSTK | lenteurs à l'ouverture des apps, partout | ✅ 0307 : `cpu_idle_latency` à 0 corrigé, attente disque ÷ 45 |
| P16 | Tampons de 36 Mo refusés (« no memory ») avec 650 Mo libres | 0307 : 82 | images manquantes possibles | 0309 : diagnostic plage d'adresses noyau « data shared » (KMA_VAONLY) |
| P17 | Coût de Mesa par dessin (0,1–0,25 ms, ~100 dessins par image) | 0303–0307 `B2L COMMIT` | le plus gros reste | 0308 : constantes Metal liées par adresse GPU (fini la copie de jusqu'à 618 Mo / 5 s) |
| P18 | ~340 textures créées et détruites par seconde dans backboardd (1,3 s de verrou Mesa par 5 s), dont des 1440×3040 RGBA16F ; touches de la Calculatrice une par une | 0309 `B2L HOLD` / `B2L TEX` | fort | 0310 : la moitié sont memoryless, vie < 500 ms ; 0311 : recyclage des memoryless |
| P19 | Premier shader de backboardd compilé en 36–48 s après chaque reconstruction de Mesa | 0305, 0306, 0309 | fausse les tests après un build Mesa | à chercher |
| P20 | Présentation synchrone : SwapEnd attend la fin du GPU (~7 ms par image, 2 s par 10 s), GPU occupé 28–41 % seulement | 0311 `B2L PERF CALLS` fb 5 | écarté | 0312 : SwapEnd asynchrone → aucun gain d'i/s, déchirures ; annulé en 0313 |
| P21 | Tampons de constantes non alignés copiés depuis de la mémoire write-combine (memcpy encore en tête) | 0311 profil | moyen | à faire |
| P22 | **Processeur bridé par notre thermique** : M4 au plancher de 936 MHz dès 65–72 °C (Samsung : contrôle à 83 °C) ; le fil d'affichage tourne 30–45 % hors des M4 | 0314/0315 | très fort | 0316 : seuils de Samsung ; 0319 : limiteur plus doux + default sur A75 |
| P24 | Gel total quand une création de 36 Mo échoue (diagnostic 0309) | 0311, 0315 | bloquant | 0316 : diagnostic retiré |
| P25 | Fuite : la purge rend des tampons encore mappés, qui ne sont jamais libérés (0306 + 0258) | 0316 | bloquant | 0317 : purge sautée si mappé → 464 refus restants ; 0318 : réserve rendue à l'échec → 3 refus |
| P26 | Coût CPU des tampons GPU : jusqu'à 2 Go de cibles ≥ 4 Mo créées par 10 s (CreateBO 1,6 s + GemClose 0,9 s) | 0318 | élevé | 0320 prévu |
| P27 | Gel total (écran figé, aucune interruption, pas de B2L HANG) : 0311, 0315, 0316, 0319 | 0319 | bloquant | piste : changement de fréquence A55 juste avant ; à confirmer |
| P28 | Morcellement de la plage partagée : refus de 19–36 Mo avec 780 Mo libres (blocs noirs, retours en arrière) | 0321 | bloquant | 0322 : gros depuis la fin |
| P29 | Adresses GPU morcelées : tas de 128 Mo refusé, Réglages plante (SIGABRT) | 0322 | bloquant | 0323 : deux zones |
| P23 | Liquid Glass trop blanc et opaque sur un fond clair (dossiers) | owner, depuis longtemps | visuel | 0325 : pas de relecture CPU ni de dual-source ; 0326 : mélange normal, aucune mesure de luminosité lancée (profil Apple1) ; 0327 : profil 1 |
| P10 | Liquid Glass | 0298 : 3–11 % des dessins | faible | LG |

## Ce qu'on a appris

- G1 : garder plus de tampons n'est pas possible tel quel, la mémoire est trop juste.
- Le tas du tiler coûte 16 Mo réels par processus, pas 128 (taille d'adresses).
- Le GPU est peu chargé : la fluidité se gagne côté processeur (pilote, log, copies).
- 0298 : le Liquid Glass ne fait que 3 à 11 % des dessins de backboardd ; ~100 dessins par image au total.
- 0299 : couper les madvise ne suffit pas ; la création de tampons (~1,5 ms chacune) devient le premier coût du pilote.
- 0297 : sans copie d'image, le frein restant est le nombre d'appels au pilote (milliers par seconde). Le `sysctl` massif n'est qu'au démarrage.
- Seuls backboardd, cameracaptured et Spotlight demandent un GPU : Réglages, Calculatrice… dessinent leur contenu au processeur (comme sur un iPhone pour la plupart des apps UIKit), backboardd compose et ajoute le Liquid Glass.

## Mesures, build après build

| Build | Plantages | « no memory » | Shaders (démarrage) | Attentes refusées | i/s accueil | i/s ouverture app |
|---|---|---|---|---|---|---|
| 0283 | 0 | 0 | — | — | — | — |
| 0285 | 2 | 137 | — | — | — | — |
| 0286 | 6 | 36 | — | — | — | — |
| 0287 | 0 | 0 | — | — | — | — |
| 0288 | 0 | 0 | 6,7 s (4,4 s de lecture du cache) | — | — | — |
| 0289 | 0 | 0 | lecture du cache 6,7 s | — | — | — |
| 0290 | 0 | 0 | 3,5 s (lecture 0,1 s) | 3412 | — | — |
| 0291 | 0 | 0 | 3,5 s | 0 | max 59,8 | — |
| 0292 | 0 | 0 | — | 0 | max 15,6 | — |
| 0293 | 0 | 34 | — | — | max 22,9 | — |
| 0295 | 0 | 0 | — | — | max 21,7 (un peu plus fluide, ressenti) | — |
| 0296 | 0 | 0 | — | — | ~15 (copie 1 image sur 2) | — |
| 0297 | 0 | 26 | 945 trouvés (0,17 s) | — | max 21,9, blit 0 (un peu plus fluide, ressenti) | — |
| 0298+0299 | 0 | 0 | — | — | max 21,8 (madvise ÷ 6 à 20) | — |
| 0300+0301 | 0 | 0 | — | — | max 24,2 (mesures : purge à chaque création) | — |
| 0302 | 0 | 0 | — | — | max 25,5 (purge 0, créations ÷ 3–4 ; aucune amélioration ressentie) | — |
| 0303 | 0 | 0 | 224 compilés (un de 48,7 s) | — | max 29,9 au début puis ralenti (mémoire GPU 344 → 1126 Mo) | — |
| 0304+0305 | 0 | 4 | — | — | ~25 stable dans la durée (propriétaire) | — |
| 0306 | 0 | 0 | — | — | moyenne ~19,5, max 33,7 (0305 : ~12) | — |
| 0307 | 0 | 82 (36 Mo) | — | — | moyenne ~18,9, max 34,9 ; disque ÷ 45 | — |
| 0308+0309 | 0 | 0 | — | — | moyenne ~23, max 41 ; UBO copiés ÷ 10–50 ; 1er shader 39,6 s (cache invalidé) | — |

## État des lieux du 2026-11-22 (après 0335, set v162)

- **Rendu** : backboardd dessine l'interface sur le Mali, 16–37 i/s (0333). Profil Metal 1 (Apple1 + Apple2).
- **Apple3** : abandonné. Pour QuartzCore, Apple3 ne change que le format XR 550 (analyse de `new_metal_context`), que notre pilote n'a pas → plantage de backboardd (0331).
- **Liquid Glass blanc** : non résolu. Les shaders, le mélange et la mesure de luminosité paraissent justes ; c'est la matrice de couleur de CA qui éclaircit. Pistes : espace colorimétrique, valeurs envoyées selon la famille.
- **Coins jaunes** : causés par la mémoire de tuile (0329). Coupée en 0334, à vérifier.
- **Clignotements** : piste principale, les créations de gros tampons (1,9 Go par 10 s en 0333). 0335 corrige le cache de Mesa, à vérifier. Il reste des refus de 35 Mo par morcellement.
- **Safari** : sans GPU depuis 0294 (pages blanches avec WebKit.GPU sur le Mali). À reprendre.
- **Vidéo matérielle (MFC Exynos)** : pas commencée ; ios-guacamole décode en logiciel, avec un correctif des couches YUV (`xnu12377-fb-yuv.diff`, MIT).
- **Publié** sur GitHub (ios-b1lte, main) le 2026-11-22 : sources GPU jusqu'à 0335 et rapports 0274–0335.
