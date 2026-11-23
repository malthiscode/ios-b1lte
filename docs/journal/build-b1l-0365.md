# B1L-0365 — luminosité : backlight-control retiré par défaut (set v168)

**Pourquoi.** Avec `backlight-control` sur AppleCLCD (0363, 0364), iOS devient inutilisable tellement il est lent, et il n'envoie jamais de luminosité dans les images (drapeau +0x35a toujours à 0).

**Changement.** `b2l_clcd_backlight` vaut 0 par défaut. La lecture du champ de luminosité reste, sans effet tant que le drapeau est à 0.

**État.** Même comportement que 0362 : système normal, curseur sans effet.

**Flash.** SYSTEM-OK, BOOT relu (BOOT-OK).
