# B1L-0321 — La réserve de textures garde les plus récentes : moins de cibles plein écran refaites (libhakiosmetal, set v152)

## Pourquoi

0318 : backboardd crée jusqu'à 2 Go de tampons ≥ 4 Mo par 10 s (139), soit ~0,9 s d'allocation et 0,3 s de mappage
GPU, plus 0,9 s de `GemClose`. Ce sont surtout les cibles de rendu plein écran (1440×3040) que CoreAnimation
refait à chaque image. La réserve de 0311/0315 n'en réutilisait que 250 sur 1 439 :
- quand elle était pleine (32 places ou 128 Mo), elle **refusait** la texture rendue ;
- les places restaient prises par de petites textures plus anciennes ;
- les grandes cibles, libérées en dernier, étaient donc détruites puis recréées à l'image suivante.

## Changement

`hakiosmetal_air.m` :
- 64 places au lieu de 32. Le plafond reste à 128 Mo.
- Quand la réserve est pleine, on **sort la plus ancienne** pour garder la nouvelle. La plus récente est la plus
  susceptible de resservir à l'image suivante.
- `B2L TXL` compte aussi `evicted` (sorties) et `drained` (réserve vidée sur refus du noyau, 0318). `rw_print`
  n'écrit que les 3 000 premières lignes de backboardd dans le journal, donc la ligne `creation refused` de 0318
  pouvait manquer.

Ce build contient aussi 0320. Mesa n'est pas reconstruit.

## À vérifier

- `reused` bien plus haut ; `B2L CREATE >=4M` et `0x42` / `0x9` en baisse ; i/s.
- « no memory » toujours ~0 (même plafond de 128 Mo).

## Résultat

Flashé (BOOT `c284989b…`, BOOT-OK), téléphone redémarré (avec 0320).

- Le propriétaire : « ça chauffe moins », pas de bridage, fluidité maintenue. Restent des blocs noirs, des éléments
  qui clignotent et de petits **retours en arrière** de l'interface.
- 0320 : A55 à 1 456 MHz, aucun changement de fréquence en marche. big 59–72 °C, M4 toujours à 2,08 GHz.
- i/s par 10 s : 19 à 42.
- Réserve : `reused` jusqu'à **2 893** par 10 s (250 en 0318), `evicted` 590–774.
- **127 « no memory »**, tous ≥ 19 Mo (36 175 872 ×42, 35 028 992 ×27…). Au premier : 781 Mo libres, 1 034 Mo de
  tampons GPU vivants, dans une plage de 2 880 Mo. Les petits tampons passent tous : c'est le **morcellement** de la
  plage partagée. → 0322.
- Une cible plein écran refusée laisse un trou noir, ou laisse à l'IOSurface le contenu d'une image plus ancienne :
  les blocs noirs, les clignotements et les retours en arrière.
