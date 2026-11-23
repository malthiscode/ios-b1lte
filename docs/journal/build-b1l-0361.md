# B1L-0361 — vibreur : diagnostic côté iOS ; luminosité étape 2 (journal seul) (set v168)

**Demande.** « Ca vibre pas dans ios relfechit et passe aussi a la partie luminosité et lien ios ».

## Vibreur : réflexion
- Le moteur marche (0359). iOS n'ouvre jamais l'« Actuator » du plug-in : il n'y a aucun `start_io` pour l'objet 7 dans les journaux 0359 et 0360.
- ios-pipa se fait passer pour un iPhone X (d22) : sur ce modèle, iOS joue les vibrations sur l'Actuator audio. Nous nous faisons passer pour un D94 (iPhone 17 Pro, vu par audiomxd).
- Le cache partagé 23G90 contient `AOPHapticsController` (AOPHaptics.framework), `AppleSPUHapticsManagerUserClient` et `ProductSupportsClosedLoopHaptics`. Sur les iPhone récents, les vibrations passeraient donc par le coprocesseur AOP (SPU), qui n'existe pas ici.
- Les journaux os_log d'iOS ne sont pas lisibles hors ligne : le texte des messages est dans le cache partagé (« compose failure »).
- **Diagnostic** (`IOUserClient.cpp`, sauvegarde `.before-0361`) : le journal note toute recherche de service IOKit qui contient haptic, AOP, SPU, actuator ou Vibrat, quel que soit le processus, 200 lignes au plus. Ce journal dira ce que iOS cherche et ne trouve pas.

## Luminosité, étape 2 du brief
- `B2LFramebuffer.cpp` (sauvegarde `.before-0361`) : service `AppleARMBacklight` sous `AppleCLCD`, avec `backlight-control` vrai et `IODisplayParameters` brightness 0..255, valeur de départ 128. C'est la forme qui marche dans ios-pipa 0154 (MIT).
- Les demandes de CoreBrightness (`brightness`, `FadePeriod`) sont **seulement notées dans le journal** (« B2L BL: … logged only »). Rien n'est envoyé à la dalle. Pour couper : `b2l_backlight=0`.
- À vérifier : les curseurs arrivent au noyau et backboardd démarre normalement.
- Sources Samsung de la dalle déjà présentes : `ref-samsung/k9820-pilotes-a412cfaf/drivers/video/fbdev/exynos/panel/s6e3ha9/` (pour l'étape 3).

**Flash.** SYSTEM-OK, BOOT relu (BOOT-OK).

**Résultat.** **Panique** (photo du propriétaire) : « Halt/Restart Timed Out @IOPlatformExpert.cpp:884 ». Le journal montre que backboardd plante deux fois (status 0xb, EXC_BAD_ACCESS) une fois le service publié, puis « rebooting due to critical process crashes: backboardd », puis le délai d arrêt dépassé. Même schéma qu en 0276. Le service AppleARMBacklight tel quel fait planter CoreBrightness avec l identité D94. Coupé en 0362.
