# B1L-0354 — batterie, étape 1 : lecture seule du MAX77705 (set v168)

**Demande.** Brief `prompt-session-haptique-lampe-batterie.md`, accord du propriétaire : « Oui, les 47 fichiers » (téléchargement, puis premier build batterie avec flash BOOT/SYSTEM et redémarrage).

## Changement
- Nouveau `xnu-overlay/iokit/Kernel/arm/B2LBattery.h`, inclus par `B2LCPU.cpp` (2 lignes : include, `b2l_battery_probe()` après `b2l_audio_probe()`). Sauvegardes `B2LCPU.cpp.before-0354` dans `xnu-overlay/` et `xnu/`.
- Thread noyau, lancé 20 s après le démarrage des cœurs, hors du chemin de démarrage. I2C par GPIO sur gpm12/gpm13 (voir le suivi pour le choix). Interroge toutes les 15 s.
- Seule transaction possible : START, adresse+W, registre d'une table fermée, START répété, adresse+R, lecture, STOP. Aucun octet de donnée n'est jamais émis. Table : PMIC 0x00/0x01 ; jauge 0x00, 0x05, 0x06, 0x09, 0x0A, 0x0B, 0x23, 0x35 ; chargeur 0xB2, 0xB3, 0xB4.
- Avant d'utiliser une ligne, elle doit relire haut ; SCL est relu juste après le front du START répété. Ligne tenue ou SCL bloqué plus de 1 ms : les deux lignes sont lâchées sans autre coup d'horloge et le pilote s'arrête jusqu'au prochain démarrage. Pas d'acquittement 3 fois de suite : arrêt.
- Broches rendues comme trouvées après chaque salve (SCL d'abord). Aucune écriture CMU, USI, HSI2C, régulateur.
- Rien n'est publié à iOS. Coupure : `b2l_batt=0` (défaut 1 dans le code). Aucune écriture de stockage nouvelle.
- Journal `B2L BATT` : 3 premières lectures, puis seulement à un changement de % entier, de câble ou d'état de charge, sinon toutes les 10 min.

## Vérifié sur le Mac
`tests/test-battery-i2c.py` compile le vrai cœur du fichier contre une puce simulée (1 pas = 1 µs) :
- les 13 registres lus juste, pour 4 états de départ des broches ; broches rendues telles quelles ;
- refus d'acquittement à chaque octet possible : aucune valeur gardée ;
- 388 260 cas de ligne tenue au sol (SCL ou SDA, à chaque instant, de 1 µs à toujours) : aucune lecture hors table, aucune écriture de registre pour une tenue de 5 µs ou plus.
- Le test a trouvé deux bugs, corrigés avant le build : START répété manqué si SCL tombe pendant le front (l'adresse+R serait devenue une donnée écrite), et débordement 32 bits de la conversion en mV au-dessus de 4,29 V.

## Non vérifié, risques
- Restent 39 cas de parasite de 3 µs ou moins tombant pile sur un front : invisibles pour tout maître I2C, matériel compris.
- Pull-up du bus supposé présent (le DTB met les broches sans pull) ; s'il manque, le pilote voit le bus bas et s'arrête.
- Jamais exécuté sur le téléphone.

## Flash
Noyau seul (`--no-libs`), set v168 inchangé. SYSTEM-OK (7 blocs sur 1750), BOOT `3337892e…` relu (BOOT-OK). Les 6 messages `B2L BATT` sont dans boot.img. Commande de redémarrage envoyée.

**Résultat (journal `out/logs/0354-boot.txt`).** Lecture réussie à chaque tour, ~14,7 ms par salve, aucune erreur. ID PMIC 0x15 rév. 02.
- Premier tour : 99,94 %, 4217 mV, câble USB, état 1 (en charge). Après débranchement : `usb=0`, puis 96,85 % et 3979 mV. TWRP juste après : 96 %, 4252 mV (rebranché). Accord à moins d'un point.
- **Constat : le chargeur passe en état 0x0B environ 30 s après le démarrage, câble encore branché.** D'après Samsung (`max77705_chg_get_wdtmr_status`), 0x0B signifie que son chien de garde a expiré : sous iOS, plus personne ne le relance (CNFG_06 WDTCLR), donc la charge s'arrête. Le courant reste négatif sur câble. Le relancer serait une écriture dans le chargeur : interdit sans accord du propriétaire.
- Rien de cassé : Wi-Fi, Bluetooth, audio, tactile et rendu journalisent comme avant ; aucune panique (seules les lignes habituelles `gPanicBase`, `DumpPanic`).
