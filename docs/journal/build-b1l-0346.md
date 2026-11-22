# B1L-0346 — Le noyau revient de l'entretien de cache EL0 (panique de 0345)

**But.** Démarrer avec le correctif de rotation de 0345.

**Symptôme (`out/logs/0345-boot.txt`, photo).** `Break 0x0001 instruction exception from kernel`, pc `0xfffffff00781d23c` dans `b2l_user_cache_op`, far `0x18e099f1c` : l'adresse que backboardd venait de corriger et dont il invalidait le cache d'instructions.

**Cause.** `patches/0108-b2l-el0-cache-maintenance.patch` insérait `b2l_user_cache_op` entre `__attribute__((noreturn))` et `handle_msr_trap`. L'attribut s'appliquait donc à la fonction insérée, qui rend un booléen : clang y mettait `brk #1` à la place du retour. Tout `IC IVAU` / `DC CVAU` venu de l'espace utilisateur faisait paniquer le noyau (désassemblage : `bl add_saved_state_pc` puis `brk #0x1`).

**Changement (noyau seulement, set v165 inchangé).** Attribut retiré dans `xnu/osfmk/arm64/sleh.c` et dans le patch 0108. `handle_msr_trap` revient désormais normalement après un entretien de cache, il ne peut plus être `noreturn`.

**Vérifié avant redémarrage.** Le patch se retire proprement (`git apply -R --check`). Dans le noyau construit, le gestionnaire avance le pc de 4 et rejoint la sortie normale de `sleh_synchronous`.

**Conséquence probable, non testée.** Les gels de Safari sur le premier `IC IVAU` de WebKit (0219–0223) passaient par cette même fonction.

**À surveiller.** Le chemin de retour n'a encore jamais tourné sur le téléphone. `patches/0057` place de la même façon un `#include` sous un `__attribute__((noinline))` : sans danger (noinline), non modifié.

**Flash.** BOOT `77330f55…` relu (BOOT-OK), SYSTEM-OK.

**Résultat.** Démarre ; Geometry Dash s'ouvre et se joue (confirmé par le propriétaire). Restent : 12 i/s avec copie de haut en bas (→ 0348) ; refus de tampons, lignes colorées et plantage de backboardd (→ 0347, 0349).
