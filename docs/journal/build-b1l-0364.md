# B1L-0364 — luminosité : champ exact de l'enregistrement (journal seul) (set v168)

**Trouvé dans le cache 23G90.** L'implémentation de `IOMobileFramebufferSwapSetBrightness` met 1 à client+0x372 et la luminosité (un `double`) à client+0x396. L'enregistrement envoyé au noyau commence à client+0x18 : le drapeau est donc à **+0x35a** et la valeur à **+0x37e**.

**Changement (`B2LFramebuffer.cpp`, sauvegarde `.before-0364`).**
- Le journal des mots changés (0363) est remplacé par une ligne « B2L BL: swap N brightness X.XXX » chaque fois que la valeur change (200 lignes au plus). Le `double` est décodé avec des entiers, car le noyau n'a pas de virgule flottante.
- `backlight-control` reste sur AppleCLCD. Rien n'est envoyé à la dalle.

**Problème connu.** La lenteur de 0363 vient sans doute de `backlight-control` : iOS attendrait une réponse de luminosité. Ce build ne la règle pas encore, il sert à lire la valeur.

**Flash.** SYSTEM-OK, BOOT relu (BOOT-OK).

**Résultat.** Toujours extrêmement lent (« ca reagiss 1000 ans plus tard »). Aucune ligne « brightness » : iOS n a jamais rempli le champ (+0x35a reste à 0). La lenteur vient de backlight-control sur AppleCLCD. Retiré en 0365.
