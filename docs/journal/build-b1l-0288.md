# B1L-0288 — G2 : compter le cache de shaders (set v136, Mesa)

Base : 0287 (pilote de 0269, arbre Mesa d'avant G1). Mesure seule, aucun changement de comportement.

## Changement (un seul)

- `apply.py`, section `B1L-0288` sur `pan_shader.c` (panfrost) : à chaque variante de shader, on compte si elle est
  trouvée dans le cache disque (B1L-0095), compilée, ou compilée sans cache, avec le temps passé. Ligne du log :
  `B2L SHADERCACHE: <processus> <pid>: N found (ms), N compiled, N without cache (ms compiling, longest ms)`,
  aux 5 premières, puis toutes les 25, et à chaque compilation de plus de 500 ms (80 lignes au plus).
- Set Metal v133 → v136 (`b2l-metal-v136` et `B2LMetalv136`).

## Pourquoi

Les 7 à 14 s de `rwa_render_create shader compile` par démarrage couvrent deux choses : la traduction AIR → NIR,
que ce cache ne garde jamais, et la compilation panfrost, qu'il garde. Cette ligne dit laquelle coûte.

## À lire

Le premier démarrage a un cache vide (le pilote est nouveau, la clé est son LC_UUID) : il faut **deux démarrages** ;
le second donne la vraie mesure.

## Gel de 0287 (enquête)

Le log de 0287 s'arrête net à 142,7 s : pas de panique, pas d'erreur GPU, pas de manque de mémoire (0 « no memory »),
températures normales (≤ 66 °C), backboardd dessine jusqu'à la dernière seconde. Le noyau s'est arrêté d'écrire, ou les
dernières secondes n'ont pas atteint la RAM avant le redémarrage forcé. Le pilote est le même que 0269. Cause non trouvée.

## Résultat

Flashé (SYSTEM 7 blocs, BOOT `ebc2ac68…`, BOOT-OK), téléphone redémarré. Chaîne `B2L SHADERCACHE` présente dans libhakiosair. Premier démarrage saccadé (cache vide), second bon (propriétaire). Log du second : 456 shaders trouvés (4401 ms), 19 compilés (491 ms) ; 0 « no memory ». Suite : 0289.
