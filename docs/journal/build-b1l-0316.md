# B1L-0316 — Seuils thermiques de Samsung, diagnostic 0309 retiré (noyau, set v150)

## Pourquoi

0315 :
- **Le M4 était bridé à 936 MHz presque tout le temps.** Notre limiteur T1 (B1L-0028) descend d'un palier par
  seconde dès 65 °C, ne remonte qu'à 60 °C, et tombe au plancher à 72 °C. La grappe tourne à 64–72 °C.
- **Gel total** quand une création de tampon de 36 Mo échoue : le diagnostic 0309 en est le suspect.

## Changement

- `B2LCPU.cpp` : seuils 83 / 78 / 95 °C (descendre / remonter / plancher), au lieu de 65 / 60 / 72.
  - Source : DTB du S10 (`exynos9820-base.dts`, thermal-zones). BIG et MID ont `*-control-temp` à 83 °C
    (0x14438 m°C) et un premier palier actif à 95 °C (0x17318). G3D a un control-temp à 88 °C ; LITTLE un premier
    palier passif à 76 °C.
  - Les boot-args `b2l_thr_hi`, `b2l_thr_lo` et `b2l_thr_crit` restent possibles.
- `B2LMaliService.cpp` : remis comme avant 0309, sans le sondage `KMA_VAONLY`. La version 0309 est gardée dans
  `.0309-diag`.

## À vérifier

- `B2L THERMAL` : le M4 reste à 2,08 GHz bien plus longtemps.
- Température, et pas de chauffe anormale.
- i/s.
- Plus de gel quand une création de tampon échoue.

## Résultat

Flashé (BOOT `aad626e1…`, BOOT-OK), téléphone redémarré.

- Le propriétaire : « tout était bien plus rapide ». M4 entre 1,4 et 2,0 GHz, big à 77–84 °C.
- i/s par 10 s : 15 à 34.
- Mais : fond noir puis disparition du Liquid Glass, un plantage, des icônes de l'écran d'accueil qui reviennent une à
  une, des blocs noirs dans la Calculatrice et des clignotements dans la bibliothèque d'apps.
- Log : **5 850 « no memory »** (3 804 de 360 Ko). Au premier refus : 798 Mo libres, 3 668 Mo disponibles,
  1 023 Mo de tampons GPU vivants. Ensuite viennent 4 fautes MMU du GPU à l'adresse 0, sur des tâches qui n'ont pas eu
  leurs tampons.
- Cause trouvée → 0317 : la purge B1L-0258 rend des tampons que Mesa garde mappés depuis 0306, et ces tampons ne
  sont jamais vraiment libérés.
