# B1L-0372 — luminosité étape 4 : curseur relié à la dalle, fondu, plancher (set v168)

**Demande.** « Oui, étape 4 ».

## Correction de 0371 d'abord
Le journal 0371 montre qu'aucune commande n'est partie. La lecture d'état PLL (`LINK_STATUS3` bit 0) valait 0 alors que l'écran était allumé : sur le 9820, cet état est dans le PHY (`DSIM_PHY_PLL_STAT0`, d'après `dsim_reg_is_pll_stable`).

La nouvelle vérification avant tout envoi :
- mode commande (CONFIG bit 18 = 0) ;
- FIFO d'en-tête et de données vides (FIFOCTRL bits 10 et 8) ;
- voie d'horloge hors ULPS (DPHY_STATUS 0x1c bit 9).

Les registres MIPI_STATUS (0x18) et DPHY_STATUS sont maintenant notés au journal.

## Changement (`B2LPanel.h`, `B2LFramebuffer.cpp`, sauvegardes `.before-0372`)

**Réception de la demande d'iOS.** `AppleARMBacklight::setProperties` ne touche jamais la dalle :
- il note la demande (`brightness-nits`, en nits 16.16) et réveille le thread du panneau ;
- `DisplayBrightnessNitsNVRAM` est la valeur qu'iOS envoie au démarrage (155 nits en 0370). Elle ne compte que tant qu'aucun `brightness-nits` n'est arrivé : c'est ce qui garde le réglage après un redémarrage.

**Thread du panneau** (2 s après le démarrage) : il relève l'état de DSIM, puis lit l'AOR de départ. Sans cette valeur, il ne relie rien et la dalle garde la luminosité du bootloader.

**Correspondance nits → AOR** :
- temps d'émission = 3064 − AOR lignes (vtotal 3064, d'après Samsung) ;
- l'AOR de départ est pris pour 420 nits ; en dessous, le temps d'émission est réduit en proportion ;
- jamais plus clair que le départ ; plancher = AOR 0x0BC9, environ 1,5 % du départ, jamais noir.

**Fondu** : chaque pas fait un quart du chemin restant (au moins une ligne), toutes les 16 ms. Chaque écriture est relue. Une nouvelle demande pendant le fondu le réoriente.

**Désactivé par défaut** : le test 3 s de 0371 ne tourne plus que si `b2l_panel_test=1`.

**Boot-args** : `b2l_panel=0` (tout coupé), `b2l_panel_nref=N` (nits supposés au départ).

## Vérifié sur le Mac
`tests/test-panel-dcs.cpp` :
- tests de 0371 ;
- pour 4 AOR de départ (de 0x0014 à 0x0BC9), sur 0 à 2200 nits : jamais plus clair que le départ, plancher tenu, courbe monotone ;
- 420 nits donne l'AOR de départ, 0 nit donne le plancher, et la moitié des nits donne la moitié du temps d'émission ;
- la valeur NVRAM est ignorée dès qu'un `brightness-nits` réel est arrivé.

## Non vérifié, risques
- Le chemin DCS n'a encore jamais tourné : si la dalle ne répond pas, le pilote se coupe et la luminosité ne bouge pas.
- Le DSC est actif sur le lien (CONFIG bit 19) ; l'effet sur les commandes n'est pas connu.
- shortcut : 420 nits supposés au départ, gamma inchangé. La moitié haute du curseur peut ne rien changer.

## Flash
Noyau seul, set v168 inchangé. SYSTEM-OK, BOOT `c7959432…` relu (BOOT-OK). Commande de redémarrage envoyée.

**Résultat.** **Le curseur change la luminosité** (le propriétaire : « Ca baisse et ca augmente », mais sur une partie du curseur seulement).
Journal `out/logs/0372-boot.txt` :
- le chemin DCS marche : power mode 0x9F, ID `a1 00 13`, AOR de départ 0x0440 ;
- valeur NVRAM au démarrage : 155 nits → AOR 0x091F en 26 pas ;
- puis chaque demande du curseur est suivie (1 pas, réorienté par la suivante).
Partie morte du curseur : au-dessus de 420 nits, la correspondance était bloquée à l'AOR de départ. Corrigé en 0373.
