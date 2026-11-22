# B1L-0301 — Mesure : durée d'un appel au pilote vue de l'app (Mesa, set v143)

## Pourquoi

0299 : le fil d'affichage de backboardd passe ~73 % de son temps processeur dans le noyau, surtout dans `mach_msg2`
(appels IOKit), alors que le pilote lui-même ne compte que quelques µs par appel (`B2L PERF CALLS`). Il faut savoir
combien coûte l'aller-retour complet (mach_msg + IOKit + pilote).

## Changement

`userspace/mesa/hakios-drm/hakios.drm.c`, `call()` : durée de chaque `IOConnectCallStructMethod`, par numéro de requête
(nombre, total, moyenne, max). Ligne toutes les 10 s par processus :
`B2L RTT: <processus> 10 s: 0x41 Nx T ms (moy µs, max µs) | ...`. Testé sur le Mac. Set v142 → v143.

## Résultat

Flashé (BOOT `4a17a9bb…`, BOOT-OK). 0300 et 0301 flashés à la suite, testés ensemble.

**Résultat** — aller-retour d'un appel ordinaire **~23–31 µs** (WAIT_BO 0x41, MMAP 0x43, SUBMIT 0x40 48–140 µs) : le trajet
mach_msg/IOKit n'est pas anormal. Total ~0,6 s par 10 s pour ces appels. **Création (0x42) : 340 à 1 030 par 10 s,
moyenne 1 à 6,5 ms, max 205 ms, 0,9 à 2,4 s par 10 s** : premier coût. Verrou Mesa de hakiosmetal (`B2L LOCK`) :
0,8 à 2,6 s d'attente cumulée par 5 s (plusieurs fils). 0 « no memory », i/s max 24,2.
