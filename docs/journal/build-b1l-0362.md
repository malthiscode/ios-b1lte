# B1L-0362 — luminosité : service coupé par défaut (set v168)

**Cause (0361).** backboardd plante deux fois (SIGSEGV) dès que `AppleARMBacklight` est publié. launchd demande alors un redémarrage, qui dépasse son délai : panique « Halt/Restart Timed Out ». La forme d'ios-pipa (identité iPhone X) ne suffit pas à CoreBrightness avec notre identité D94.

**Changement.** `B2LFramebuffer.cpp` : `b2l_backlight` vaut 0 par défaut ; le service n'est plus publié. Le diagnostic haptique de 0361 reste.

**Suite luminosité.** Trouver dans le cache partagé 23G90 ce que CoreBrightness lit sur ce service pour un D94, avant de le republier.

**Flash.** SYSTEM-OK, BOOT relu (BOOT-OK).

**Résultat.** Démarre (propriétaire : « Oui, il démarre »). Diagnostic haptique : aucun processus ne cherche de service haptic/AOP/SPU (seulement SpringBoard → AppleSPUTimesync, absent). Aucun start_io sur l Actuator. Le refus de vibrer se décide plus haut que IOKit.
