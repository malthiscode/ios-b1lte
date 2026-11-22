# B1L-0295 — quels appels système occupent le fil de rendu (noyau, mesure seule)

## Pourquoi

0293 : les défauts de page ont presque disparu, mais le fil de rendu de backboardd reste ~62 % dans le noyau, surtout
dans des appels système (`esr 0x56000080`, `svc #0x80`, ~98 000 échantillons), via libsystem_kernel. Le profileur ne
dit pas lesquels. Écartés en vérifiant : l'attente GPU (`hakios_mali_wait` dort, `lck_mtx_sleep_deadline`) et la console
texte du noyau (17 507 échantillons avant l'interface, 217 après : un coût de démarrage, pas de fluidité).

## Changement (un seul)

`osfmk/arm64/b2l_prof.c` : pour chaque échantillon de backboardd pris dans un appel système (EC 0x15), le numéro
d'appel (`x16` de l'état utilisateur sauvé ; < 0 = piège Mach) est compté ; ligne `B2L PROF SVC: x16 N: M kernel
samples` toutes les 10 s (≥ 8). Rien d'autre ne change. Noyau seul (`--no-libs`), set v140.

## Résultat

Propriétaire : Safari remarche (0294), fluidité « un peu meilleure, pas énorme ». 0 « no memory », 0 plantage ; GPU :
backboardd et cameracaptured seulement. Échantillons noyau de backboardd par appel système (somme du démarrage) :
`mach_msg2_trap` (Mach −47 : messages Mach, dont **tous les appels aux pilotes IOKit**) **94 868** ; `workq_kernreturn`
(368) 36 078 ; `madvise` (75) 9 571 ; `sysctl` (202, dont nos lignes `kern.hakios.say`) 6 729 ; `kevent_id`/`kevent_qos`
(375/374) 9 401 ; `psynch_mutexwait`/`drop` (301/302) 6 527 ; `msync` (65) 3 886 ; `bsdthread_ctl` (478) 2 168.
Noms lus dans `bsd/kern/syscalls.master` et `osfmk/kern/syscall_sw.c`. Le temps par appel de pilote existe
(`B2L PERF CALLS`, B1L-0102) mais n'est écrit qu'avec `b2l_perf=1`, absent des boot-args. Suite : 0296.
