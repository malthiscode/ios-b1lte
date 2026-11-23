# B1L-0363 — luminosité étape 2 : le chemin d'iOS 26 (journal seul) (set v168)

**Pourquoi 0361 plantait.** Le cache partagé 23G90 ne contient plus ni `AppleARMBacklight` ni `IODisplayParameters` : la forme d'ios-pipa date d'iOS 16. QuartzCore (dans backboardd) cherche l'écran par `IOPropertyMatch` sur `backlight-control`, à côté de `OLEDDisplay` et `SynchronousBrightnessTransactions`. Il a pris notre service à part pour un framebuffer et a planté.
IOMobileFramebuffer d'iOS 26 a `SwapSetBrightness`, `SwapSetBrightnessLimit` et `GetBrightnessControlCapabilities` : la luminosité voyage dans l'enregistrement de chaque image (sélecteur 5, 0x588 octets).

**Changement (`B2LFramebuffer.cpp`, sauvegarde `.before-0363`).**
- `backlight-control` vrai sur `AppleCLCD` lui-même. Pour le couper : `b2l_clcd_backlight=0`.
- Journal « B2L BL: swap … record +0x… » : les mots de l'enregistrement qui changent hors des champs connus, 300 lignes au plus. Il montrera où arrive le curseur.
- Journal de tout `setProperties` sur AppleCLCD, 60 lignes au plus.
- Rien n'est envoyé à la dalle.

**Risque.** ios-pipa notait que `backlight-control` sur AppleCLCD bloquait backboardd (sur iOS 16). Si l'écran ne démarre pas : `b2l_clcd_backlight=0`, ou le build 0362.

**Flash.** SYSTEM-OK, BOOT relu (BOOT-OK).

**Résultat.** Démarre sans planter, mais iOS est **très lent**, inutilisable (propriétaire). SpringBoard s arrête deux fois (status 6). Le journal des mots changés a atteint ses 300 lignes avant le curseur, à cause du bruit image par image. Nouveau sélecteur appelé : 74 (FrameInfoDescriptor, répond 0).
