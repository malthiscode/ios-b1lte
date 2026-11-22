# B1L-0336 — Nouveau fond d'écran forcé (fond sombre, image du propriétaire)

## Pourquoi

Demande du propriétaire : forcer son nouveau fond d'écran (formes lumineuses arc-en-ciel sur fond noir).
Il se demande aussi si la façon de forcer le fond d'écran explique le verre trop blanc.

Rappel du mécanisme (`userspace/b2l-springboard.m`, v5, depuis B1L-0027/0125) : PosterBoard ne dessine rien.
`b2l-springboard` pose donc une `UIImageView` (photo intégrée à la bibliothèque, section `__DATA,__b2l_wall`)
PAR-DESSUS la vue distante du poster, dans chaque fenêtre « Wallpaper », et derrière tout dans `SBHomeScreenWindow`.
SpringBoard ne reçoit donc jamais l'état du poster (couleur moyenne, luminosité, réglages de lisibilité).
Les logs de 0259 le montrent : le style d'icône attend cet état, qui n'arrive jamais. Le verre se règle peut-être de la
même façon sur une valeur par défaut « fond clair ». Le fond sombre permet de le tester : si le verre devient correct,
la piste se confirme.

## Changement

- `userspace/wallpaper/fond-proprietaire.jpg` : nouvelle image, mise à 1440×3120 (sips). L'ancienne photo est gardée
  dans `fond-proprietaire-0125.jpg`.
- `xnu-overlay/bsd/kern/b2l_springboard_payload.h` regénéré par `scripts/build-springboard-dylib.py`
  (sauvegarde `.before-0336`) ; le noyau remplace la bibliothèque sur le téléphone si son contenu a changé.
- Noyau seul (`--no-libs`), set v162 inchangé.

## À vérifier

- le nouveau fond s'affiche (écran d'accueil et écran verrouillé) ;
- le verre (dock, dossiers, Centre de contrôle) : toujours blanc, ou teinté sombre ?

## Résultat

BOOT-OK (boot df18fbea…). **Propriétaire : le nouveau fond n'apparaît pas.** Cause : `b2l_apps_publish` ne remplace jamais un fichier déjà présent (il vérifie seulement qu'il est identique) ; l'ancienne `b2l-springboard-v5.dylib` est restée. Corrigé en 0337 par un nouveau nom (v6).
