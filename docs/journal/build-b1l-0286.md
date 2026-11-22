# B1L-0286 — retour à 2 s pour les tampons inutilisés (correction de 0285)

## Pourquoi

0285 a épuisé la mémoire du téléphone (voir build-b1l-0285.md) : backboardd gardait jusqu'à 256 Mo de tampons inutilisés, la mémoire
libre est tombée à 27 Mo et backboardd a planté.

## Changement (Mesa, `apply.py` section B1L-0286, sauvegarde `.before-0286`)

- `B2L_BO_IDLE_S` : 30 → **2** (la règle d'origine de Mesa). Le budget de 256 Mo reste comme plafond, mais il ne devrait plus servir.
- Restent de 0285 : le mappage gardé pour les tampons en réserve (aucune mémoire en plus) et la ligne `B2L BOCACHE`.
- Set v134 → v135.

## Mesures attendues

- Plus aucun « no memory for a buffer », pas de redémarrage de backboardd.
- `B2L BOCACHE` : part réutilisée avec 2 s, à comparer aux 93 % de 0285.

## Installé

- SYSTEM-OK, BOOT `3362c1d39369a4c54718fd38acc103ec6f9d62ecfcb5ef2469b391defb1b4382`, BOOT-OK ; plug-in `B2LMetalv135`.

## Résultat

- En attente.
