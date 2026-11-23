# B1L-0366 — luminosité : service complet pour BrightnessControl d'iOS 26 (journal seul) (set v168)

**Analyse du cache 23G90 (Mac).**
- La luminosité d'iOS 26 passe par `BrightnessControl.framework` (`BCAppleBacklightBrtControl`), chargé par IOMobileFramebuffer. Le framework trouve le service par `IOPropertyMatch` `backlight-control`.
- `initWithService:` lit `IODisplayParameters` : `brightness`{min,max}, `BrightnessMilliNits`{min,max,value}, `BrightnessMicroAmps`{min,max}. Il cherche aussi en option `dfr-brightness-control` et `dcp-brightness-node`.
- Une clé absente donne un nombre NULL : c'est le SIGSEGV de backboardd en 0361 (seul `brightness` était publié).
- La luminosité s'écrit par `IORegistryEntrySetCFProperties` sur ce service (« Setting nits to %f », « Unable to set CF properties »).
- IOMobileFramebuffer garde la luminosité « par image » (`SwapSetBrightness`, champ +0x37e) pour les écrans à contrôle synchrone. Mettre `backlight-control` sur AppleCLCD rendait iOS très lent (0363, 0364).

**Changement (`B2LFramebuffer.cpp`, sauvegarde `.before-0366`).**
- `AppleARMBacklight` est republié par défaut, avec `brightness` 0..255, `BrightnessMilliNits` 2000..500000 et `BrightnessMicroAmps` 0..1000. La plage en nits est provisoire : la vraie viendra de la table de la S6E3HA9, à l'étape 3.
- Toutes les propriétés que iOS écrit sont notées (« B2L BL: … sets … (logged only) », 80 lignes au plus). Rien n'est envoyé à la dalle.
- `backlight-control` reste absent d'AppleCLCD.

**Si backboardd plante encore** : boot-arg `b2l_backlight=0`, ou le build 0365.

**Flash.** SYSTEM-OK, BOOT relu (BOOT-OK).

**Résultat.** **Panique** (même « Halt/Restart Timed Out »). backboardd SIGSEGV dans CoreBrightness `__DisplaySliderToLogicalBrightnessInternal+0x114` (pile : findBacklight → CBDisplayModuleiOS initWithBacklight → DisplayCreateWithCapabilitiesAndID) : la table du curseur (display+72) est NULL. BrightnessControl la lit dans `backlight-marketing-table`. 0367 : service coupé (BOOT-OK).
