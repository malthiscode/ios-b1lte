# B1L-0371 — luminosité étape 3 : commandes DCS à la dalle, AOR moyen 3 s puis retour (set v168)

**Demande.** « Oui, étape 3 » : envoyer une seule fois un niveau moyen fixe, puis revenir seul au niveau de départ après 3 s.

## Changement
- Nouveau `xnu-overlay/iokit/Kernel/arm/B2LPanel.h`, branché dans `B2LFramebuffer.cpp` (sauvegarde `.before-0371`) juste après la publication d'AppleARMBacklight.
- **Chemin DCS unique** vers la S6E3HA9 par DSIM0 (0x19080000), en mode commande comme S-Boot l'a laissé :
  - sous un verrou (`b2l_dsi_lock`, à partager plus tard avec l'écran éteint), sur son propre thread, jamais dans le chemin des swaps ;
  - chaque attente est bornée (50 ms par paquet, 100 ms par lecture) et dort (`IOSleep`), interruptions actives.
- **Liste fermée** : lectures (taille max 0x37, lecture DCS 0x06) et trois écritures longues 0x39 seulement :
  - clé niveau 2 `F0 5A 5A` / `F0 A5 A5` ;
  - AOR `B1 hi lo`, valeur bornée à la table AOR normale de Samsung (0x0014..0x0BC9, sans HBM, jamais niveau 0) ;
  - mise à jour gamma `F7 03`.
  - Pas de 0x51, pas de HBM, pas de registre MTP ou mémoire flash. Toute autre commande est refusée avant de toucher la FIFO.
- **Première anomalie = DCS coupé** pour de bon (sauf les essais de remise de l'AOR de départ).
- Test (~60 s après le démarrage) :
  1. relevé DSIM (link0/1/3, config, intsrc, intmsk, fifo) ; arrêt si PLL instable ou mode vidéo ;
  2. lecture power mode (0x0A) et ID (0x04) ;
  3. lecture de l'AOR laissé par S-Boot (0xB1) — **s'il n'est pas lu, rien n'est écrit** ;
  4. AOR moyen 0x0601 (table beyond1 a3 s0, index 31), mise à jour gamma, relecture ;
  5. 3 s, puis AOR de départ remis et relu (3 essais).
- Boot-args : `b2l_panel=0` coupe tout, `b2l_panel_test=0` garde seulement les lectures, `b2l_panel_delay=N` (secondes).

## Pourquoi l'AOR et pas 0x51
Le brief demande de ne pas supposer que 0x51 suffit. Chez Samsung, la S6E3HA9 règle sa luminosité par gamma + AOR + ELVSS, etc., verrouillés par `F7 03`. L'AOR seul (rapport d'émission) change la luminosité sans toucher au gamma.

## Vérifié sur le Mac
`tests/test-panel-dcs.cpp` (registres DSIM simulés) :
- les 65 536 valeurs AOR : acceptées seulement dans 0x0014..0x0BC9 ;
- 0x51, 0x53 (HBM) et une clé erronée sont refusés sans rien écrire ;
- l'empaquetage est correct (`B1 06 01` → mot 0x0106B1, en-tête 0x0339) ;
- une FIFO bloquée coupe le pilote.

## Non vérifié, risques
- La lecture (BTA) n'a jamais tourné sur ce téléphone ; si la dalle ne répond pas, le pilote se coupe et la luminosité reste celle de S-Boot.
- L'effet d'un AOR seul avec le gamma de S-Boot n'est pas mesuré ; l'écran devrait baisser d'environ moitié.

## Flash
Noyau seul, set v168 inchangé. SYSTEM-OK, BOOT `64b5170f…` relu (BOOT-OK). Commande de redémarrage envoyée.

**Résultat.** Le propriétaire a vu l'écran baisser puis revenir, **mais le journal (`out/logs/0371-boot.txt`) montre qu'aucune commande n'est partie** : `LINK_STATUS3` = 0 → « PLL not stable » → DCS coupé avant toute lecture. La baisse vue venait d'autre chose (sans doute iOS lui-même). Cause : sur le 9820, l'état du PLL est dans un registre du PHY (`DSIM_PHY_PLL_STAT0`), pas dans `LINK_STATUS3`. État relu : config 0x04887cff (mode commande, DSC actif), FIFO 0x1507 (vides), link1 0x0600068f. Corrigé en 0372.
