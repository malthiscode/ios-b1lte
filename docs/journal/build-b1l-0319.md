# B1L-0319 — Le CPU ne bride plus l'interface : limiteur thermique plus doux, M4 laissés à l'interface (noyau, set v151)

## Pourquoi

Le propriétaire : l'interface est bridée par le CPU ; il faut l'optimiser ailleurs sans retirer les limites
thermiques.

0318 :
- À 83–84 °C, le limiteur baisse le M4 d'un palier par seconde tant que la sonde reste ≥ 83 °C. La sonde est en
  retard sur la baisse, donc le M4 tombe de 2 080 à 936–1 144 MHz, puis remonte : la fluidité varie.
- L'ordonnanceur envoie sur les deux M4 tout ce qui est interface, mais aussi toute la QoS « default » de toutes
  les apps et de tous les services. C'est autant de chaleur sur les M4 et de concurrence pour le fil d'affichage de
  backboardd.
- CPU aussi pris par la mémoire GPU : `B2L CREATE` montre jusqu'à **2 Go de tampons ≥ 4 Mo créés en 10 s** (139),
  0,9 s d'allocation et 0,3 s de mappage GPU ; `GemClose` 0,9 s. Ce sont les cibles plein écran que CoreAnimation
  refait sans cesse → piste suivante (0320).

## Changement

- `B2LThermal.h`, `b2l_thr_step` : à ≥ 83 °C, on ne baisse d'un palier que si la température n'a pas baissé depuis
  la seconde d'avant. Si elle baisse déjà, on garde le palier. Le seuil de 95 °C (plancher immédiat) ne change pas.
  Petit test sur Mac : 8 cas OK.
- `B2LCPU.cpp` : on remonte à ≤ 80 °C au lieu de 78 °C.
- `sched_clutch.c`, `b2l_sched_initial_preferred_pset` : la QoS default va sur les A75. Les M4 gardent l'interface,
  la QoS user-initiated et la priorité fixe.

## À vérifier

- `B2L THERMAL` : le M4 reste plus haut (~1,6–2,0 GHz) et la température reste ≤ 85 °C ; jamais 95 °C.
- `B2L SCHED: preferred … default=1`.
- `B2L PROF T` : le fil d'affichage de backboardd plus souvent sur M4 ; i/s plus réguliers.

## Résultat

Flashé (BOOT `4d733d40…`, BOOT-OK), téléphone redémarré. Session courte (~2 min d'usage), finie par un plantage.

- `B2L SCHED: preferred foreground=2 default=1` : la QoS default est bien sur les A75.
- Fil d'affichage de backboardd : **74–79 % des échantillons sur M4** (55–68 % en 0315), 2–3 % sur A55.
- Thermique : le M4 tient 1,8–2,0 GHz plus longtemps, mais la température plafonne à 84–86 °C sans redescendre, et
  le M4 finit quand même à 936 MHz. Les A55 montent aussi à 84–86 °C. La chaleur vient de la charge totale ; le
  limiteur fait son travail. → réduire le travail CPU (P26).
- 0 « no memory », 0 faute GPU, 0 panique.
- **Plantage** : le journal s'arrête net (dernière ligne : thermique A55 85,9 °C), sans panique ni chien de garde. Les
  `0x41 -> 0xe00002d6` de la fin sont des sondages normaux (timeout 0), présents dans tous les boots. Cause non
  trouvée dans le journal.
  Le propriétaire : **écran figé**, sans redémarrage. Le détecteur de gel B1L-0200, qui dessine `B2L HANG` à l'écran
  après 6 s sans écriture du journal, ne s'est pas montré. Plus aucun cœur ne prenait d'interruption : c'est un
  blocage matériel (bus, tension ou fréquence), pas un interblocage logiciel. Dernier événement : passage des A55 à
  1 456 MHz (ACPM, ret 0). Le nombre de changements de fréquence par boot (A55 4–28) ne suit pas les gels, donc ce
  n'est qu'une piste.
- i/s : 14, 227, 265 sur les dernières fenêtres de 10 s.
