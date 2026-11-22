# B1L-0278 — fil d'IO temps réel du HAL (compteur 26 MHz) + Bluetooth

## Pourquoi

Source : os_log d'audiomxd de 0277, image `out/live/ios26-userdata-audio-277.img`, lue en lecture seule.

- Le produit est reconnu : « ProductID to int is: 43 », et il n'y a plus d'erreur de table de routage.
- Un son système démarre bien : le client `CA_UISoundClient` lance l'IO sur « 114 Speaker (VAD AggDev) ».
- Mais :
  - `AMCP::Utility::Thread_Utilities::configure_thread_for_realtime - thread_policy_set failed. Cycle Size: 0, Thread Parameters: 1200 1200 1200` ;
  - « HALS_IOThread: Failed to change thread to realtime priority » ;
  - puis « AQMEIO timed out after 3.000s (0 0) » et « client stopping after failed start ».
  Notre plug-in ne reçoit jamais `StartIO` : 0 `start_io` dans le journal.
- Cause : le compteur du S10 tourne à 26 MHz (`CNTFRQ=26000000`), celui d'un iPhone à 24 MHz.
  - XNU refuse un calcul temps réel sous `min_rt_quantum` = 50 µs, soit 1300 tics chez nous.
  - Le HAL demande 1200 tics, c'est-à-dire 50 µs à 24 MHz.
  - `thread_policy_set` répond donc `KERN_INVALID_ARGUMENT`.

## Changement

- `osfmk/kern/sched_rt.c` (`sched_realtime_timebase_init`) : sur B2L, `min_rt_quantum` passe à 40 µs (1040 tics). 1200 est alors accepté.
  - Copie dans `xnu-overlay/osfmk/kern/sched_rt.c` ; l'ancienne version est dans `~/Developer/s10-port/audio-next-parked/sched_rt.c.before-0278`.
- Bluetooth (session Bluetooth, noyau seulement) : `B2LBluetooth.cpp`. Une commande propriétaire que la puce ne connaît pas reçoit une réponse « succès », et les écritures passent entières. Coupure : `b2l_bt_vsc=0`.
- Le reste ne change pas par rapport à 0277 : `product-id` pour audiomxd seul, set Metal v133, plug-in v2.
- Rien sur USERDATA.

## À vérifier

- Plus de « thread_policy_set failed » dans audiomxd.
- `hakios: trace: start_io` / `do-io`, et un son au clavier, à la sonnerie ou au verrouillage.

## Installé

- SYSTEM-OK, BOOT `0924d8d95acad24a6192fc4121767d7710e670e480e3239eb69995bb281154ed`, BOOT-OK. Noyau non strippé : `out/mach-0278.unstripped`.
- `sched_rt.o` est recompilé : `mov w0, #40` dans `sched_realtime_timebase_init`.
- Téléphone redémarré vers le système. Journal à tirer : `out/logs/0278-boot.txt`.
