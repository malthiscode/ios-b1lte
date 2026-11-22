# B1L-0331 — Classes de taille pour les gros tampons GPU (noyau, set v158)

## Pourquoi

Les refus de 0325–0327 portent sur des cibles de tailles toutes différentes (26,2 ; 27,9 ; 28,0 ; 30,3 ; 30,6 ; 35,0 ;
36,2 Mo). Un trou laissé par une cible libérée ne servait pas à la suivante, un peu plus grande.

## Changement

`xnu-overlay/iokit/Kernel/arm/B2LMaliService.cpp` (sauvegarde `.before-0331`), `createBO` : un tampon de 8 Mo ou plus
(hors tas du tiler) est alloué arrondi au multiple de 4 Mo supérieur (au plus 4 Mo perdus). La taille vue par le GPU et
par Mesa ne change pas. Le message « heap of » n'est écrit que pour un tampon moins adossé que sa taille.

## À vérifier

- « no memory » proche de 0 ; pas de plantage ni de faute GPU.

## Résultat

Flashé (BOOT `10d03474…`, BOOT-OK), redémarré avec 0328–0330. Propriétaire : rendu CPU. Log : en profil 4 (Apple1–3 = 1, Apple4+ = 0, confirmé par `B2L FAMILY`), CoreAnimation demande des pipelines au format 550 (BGRA10_XR) et 540 que la bibliothèque refuse : backboardd s’arrête deux fois (namespace 13), puis l’interface passe au rendu processeur. 0329–0331 ne sont donc pas évalués → 0332 revient au profil 1.
