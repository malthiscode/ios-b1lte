# B1L-0368 — luminosité : table du curseur (journal seul) (set v168)

**Cause (0366).** Pile de backboardd (symbolisée avec dscsym/dscname) : `-[CBDisplayContaineriOS findBacklight]` → `-[CBDisplayModuleiOS initWithBacklight:…]` → `DisplayCreateWithCapabilitiesAndID` → `__DisplaySliderToLogicalBrightnessInternal+0x114`, qui lit une table de flottants NULL. BrightnessControl remplit cette table depuis la propriété `backlight-marketing-table` du service (données, u32 en 16.16 nits).

**Changement (`B2LFramebuffer.cpp`, sauvegarde `.before-0368`).**
- `backlight-marketing-table` sur AppleARMBacklight : les 101 valeurs de DeviceTree.d94ap 23G90 `/backlight` (1 à 2175 nits).
- `BrightnessMilliNits` 1000..1060000 (`user-accessible-max-nits` du d94ap = 1060).
- Le service est republié par défaut. Rien n'est envoyé à la dalle : les demandes sont seulement notées.
- 0367 (entre les deux) : service coupé, pour sortir de la panique.

**Flash.** SYSTEM-OK, BOOT relu (BOOT-OK).

**Résultat.** **Panique**, même SIGSEGV au même endroit. La table n était pas la cause directe : Display.m appelle d abord `-[CBBacklightNode getBacklightCalibration:]`. Si ce dictionnaire `backlight-calibration-parameters` manque, il prend des valeurs par défaut et convertit avec une table pas encore chargée. 0369 : service coupé.
