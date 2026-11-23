# B1L-0370 — luminosité : calibration du rétroéclairage (journal seul) (set v168)

**Cause (0368).** `DisplayCreateWithCapabilitiesAndID` (CoreBrightness) appelle `[backlight getBacklightCalibration:]`. Sans le dictionnaire `backlight-calibration-parameters` sur le service, il passe aux valeurs par défaut et convertit avec une table pas encore chargée : SIGSEGV dans `__DisplaySliderToLogicalBrightnessInternal`.

**Clés lues** (`-[CBBacklightNode getBacklightCalibration:]`, entiers en 16.16) :
- `lMaxProduct`, `lMidProduct`, `lMinProduct` ;
- `lMaxPanel`, `lMidPanel`, `lMinPanel` ;
- `current-for-max-backlight`, `current-for-mid-backlight`, `hardware-max-current-limit`, `milliAmps2NitsScaleFactor`.

**Changement (`B2LFramebuffer.cpp`, sauvegarde `.before-0370`).**
- Dictionnaire publié. Les luminances viennent du d94ap (2175, 155, 1 nits).
- Les courants (20, 2 et 25 mA, 108,75 nits/mA) sont **inventés**, car aucun registre n'en dépend encore. À revoir si les niveaux d'iOS paraissent faux.
- Le service est republié par défaut. 0369 (entre les deux) : service coupé. Toujours journal seul.

**Flash.** SYSTEM-OK, BOOT relu (BOOT-OK).

**Résultat.** **Étape 2 réussie** : pas de plantage, système normal. iOS envoie au service `brightness-nits` (16.16, par ex. 0xa56b2f ≈ 165 nits, monte avec le curseur), `DisplayBrightness` {Nits, NitsPhysical, Brightness}, `DisplayBrightnessNitsNVRAM`, `brightness-nits-cap` 2175, `CompensationTweak` ; et `IOMFBBrightnessLevel` (même valeur) sur AppleCLCD.
