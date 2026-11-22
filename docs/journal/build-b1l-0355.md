# B1L-0355 — batterie, étape 2 : la batterie donnée à iOS (set v168)

**Demande.** « Oui lit le journal et si ca marche connecte ca a ios ». 0354 a marché (voir `build-b1l-0354.md`).

## Changement
- `B2LBattery.h` (sauvegarde `.before-0355`) : classe `B2LBatterySource : IOPMPowerSource`, créée au premier tour valide, rattachée à la plateforme puis `registerService()`. Même schéma que Jesvi-iOS `RedwoodBattery.cpp` (APSL 2.0) ; ios-pipa 0062 (MIT) pour les clés. Rien d'autre ne change.
- Publié : pourcentage entier arrondi vers le bas (comme Android), MaxCapacity 100, tension, courant moyen, câble (CHGIN_OK ou WCIN_OK), en charge (états 0–2), plein (3–4 → 100 %), mAh de la jauge, DesignCapacity 3400 (DTB), AdapterDetails FamilyCode USB inconnu ou déconnecté.
- Seuls les tours qui passent `b2l_batt_sane` (tension 2,5–4,6 V, batterie présente) sont publiés : une lecture ratée ne devient jamais 0 %.
- **Pas de température** (étape 3). Pas de durée restante.
- Aucune écriture dans la jauge ni le chargeur. Aucune écriture de stockage nouvelle.

## Vérifié sur le Mac
`tests/test-battery-i2c.py` : en plus de 0354, la vue publiée à partir des vraies valeurs du journal 0354 (96 %, sur câble, en charge ; chien de garde expiré → branché pas en charge ; plein → 100 % ; débranché).

## Non vérifié, risques
- Le binaire powerd de 23G90 n'est pas extrait sur le Mac : que powerd prenne une sous-classe d'`IOPMPowerSource` vient de Jesvi (iOS 16.5), pas d'iOS 26.
- Sur câble, iOS affichera « pas en charge » quelques secondes après le démarrage : c'est la vérité (chien de garde du chargeur, voir 0354).
- Si le pilote s'arrête (bus tenu, 3 échecs), iOS garde la dernière valeur.
- Sans température, thermalmonitord pourrait réagir : à surveiller.

## Flash
Noyau seul, set v168 inchangé. SYSTEM-OK, BOOT `6521a073…` relu (BOOT-OK), pilote présent dans boot.img. Commande de redémarrage envoyée.

**Résultat.** **Réussi** : le propriétaire voit le vrai pourcentage dans iOS. Journal `0355-boot.txt` : « given to iOS, 98% charging », câble suivi, puis état 0x0B (chien de garde) comme en 0354 ; aucune panique.
