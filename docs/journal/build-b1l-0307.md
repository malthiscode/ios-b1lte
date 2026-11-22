# B1L-0307 — Latence de réveil des cœurs renseignée : les petits `delay()` attendent activement (noyau, set v146)

## Pourquoi

0306 : 18 000 échantillons de backboardd dans `b2l_ufs_transport_read` → `command` → `delay(1)` →
`assert_wait_deadline` + `thread_block`. XNU (`_clock_delay_until_deadline_with_leeway`, osfmk/kern/clock.c) n'attend
activement que si `ml_delay_should_spin` (osfmk/arm64/machine_routines.c) le dit, c.-à-d. si le délai est plus court
que `cpu_idle_latency`. Celle-ci vient de `ml_processor_info_t.powergate_latency`, que `B2LCPU::registerProcessor` ne
remplissait pas (0) : **tout `delay()` du noyau s'endort**, même 1 µs. Le pilote UFS sondait donc le contrôleur en
s'endormant et se réveillant à chaque microseconde — à chaque lecture de fichier.

## Changement

`B2LCPU.cpp` : `info.powergate_latency = 20000` (ns). Les `delay()` de moins de 20 µs attendent activement. Valeur
choisie par nous (aucune valeur Samsung ni Apple lue). Effet secondaire connu (osfmk/arm/rtclock.c) : un cœur au repos
se réveille jusqu'à 20 µs avant son minuteur et attend activement le reste. Noyau seul.

## À vérifier

KSTK : `b2l_ufs_transport_read` / `delay_for_interval` presque disparus ; ouverture des apps ; i/s ; démarrage normal ;
aucun gel.

## Résultat

Flashé (BOOT `4726de75…`, BOOT-OK). Démarrage normal, pas de gel, aucun plantage de backboardd.
**Attente disque : 18 350 → 410 échantillons** en régime normal ; échantillons noyau de backboardd 80 400 → 30 600.
i/s par 10 s : 8,4 à 34,9 (moyenne ~18,9, comme 0306 : le reste du temps est ailleurs). Restent : réveils de fils
GCD (`Call_continuation` + verrous workq) ~16 000, attente du GPU (`hakios_mali_wait`) 3 850, vidage de cache dans
`hakios_mali_unmap` 1 860. `B2L COMMIT` : Mesa draw 0,1 à 4,1 s par 10 s.
**82 « no memory » pour des tampons de 36 175 872 octets** avec 640–680 Mo libres, 953 Mo de tampons GPU vivants
(0306 : 0, 0305 : 4, 0293 : 34, 0297 : 26) : allocation de 36 Mo d'un bloc qui échoue malgré la mémoire libre ;
cause pas encore cherchée.
