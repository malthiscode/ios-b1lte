# B1L-0320 — A55 fixés à 1 456 MHz, sans changement de fréquence en marche (noyau, set v151)

## Pourquoi

- 0319 : gel total, écran figé. Plus aucune interruption : le détecteur B1L-0200 ne s'est pas montré. Le dernier
  événement est un changement de fréquence des A55 (1 586 → 1 456 MHz par ACPM) (P27). C'est une piste, pas une
  preuve.
- Les A55 montaient à 84–86 °C, à 1 846 MHz.
- Depuis 0319, les A55 ne font plus que le travail de fond (QoS background) : l'interface n'en dépend pas.
- Le propriétaire : « fait les deux », donc la piste A55 (0320) puis le coût des tampons GPU (0321).

## Changement

`B2LCPU.cpp` :
- Cible de démarrage des A55 : 1 846 → **1 456 MHz**, un palier de leur fvmap déjà utilisé sans souci par le
  limiteur en 0319.
- Limiteur : les A55 restent à cette cible. Ils ne descendent au plancher qu'à 95 °C (seuil critique, sur la zone la
  plus chaude) et reviennent à la cible à ≤ 80 °C. Plus de petits pas sur cl0. A75, M4 et GPU sont inchangés.

## À vérifier

- `B2L DVFS: cl0 A55 request 1456000 kHz -> ret=0`.
- Aucune ligne `B2L THERMAL: cl0 A55 … ->` en usage normal ; zone little plus froide.
- Plus de gel total sur plusieurs sessions (les gels sont rares, il faut du temps pour conclure).

## Résultat

Flashé (BOOT `f227fb01…`, BOOT-OK). Testé avec 0321 par-dessus.
