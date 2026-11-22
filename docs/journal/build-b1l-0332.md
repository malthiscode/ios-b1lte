# B1L-0332 — Retour au profil Metal 1 (userspace, set v159)

## Pourquoi

0331 (avec 0328) : au profil 4 (Apple1–3), CoreAnimation crée ses pipelines d'écran au format `MTLPixelFormat` 550
(BGRA10_XR, 10 bits étendus, Apple3 et plus) et 540, que la bibliothèque ne connaît pas : backboardd s'arrête deux
fois et le système passe au rendu processeur.

## Changement

`hakiosmetal_air.m` (sauvegarde `.before-0332`) : `featureProfile` par défaut 4 → 1 (Apple1–2, comme 0327, qui lance
déjà `compute_average_luma`). 0329 (mémoire de tuile), 0330 (réserve) et 0331 (classes de taille) restent.

## À vérifier

- rendu GPU revenu ; `B2L LUMA` ; « not written back » ; « no memory » ; clignotements.

## Résultat

Flashé (BOOT `261855bb…`, BOOT-OK). Rendu GPU revenu. Propriétaire : verre toujours blanc, et des **coins jaunes** autour des éléments de verre et du texte (régression de 0329). Log : « no memory » 32 → 6 ; évictions presque nulles (0330) ; jusqu’à 8 531 cibles non réécrites par 10 s (0329). `B2L LUMA` : une valeur lisible (somme 767–779 pour 800 échantillons, soit ~0,96 pour un fond beige clair), les autres sont du bruit (tampon réutilisé par CA avant la lecture). La mesure semble juste : c’est la matrice de CA (décalage +0,425) qui blanchit le clair.
