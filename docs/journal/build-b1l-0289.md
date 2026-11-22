# B1L-0289 — G2 : où partent les 10 ms par shader trouvé (set v137, Mesa)

## Résultat

Flashé (BOOT `67a6d34c…`), démarré deux fois par le propriétaire. Log du second : `416 found (6526 ms), 9 compiled
(289 ms); key 2 ms, read 6716 ms`. **La lecture dans la base de Mesa prend tout** (~16 ms par recherche, 196 ms pour
la première) ; la clé ne coûte rien. 0 « no memory », pilote v137 chargé. Suite : 0290.
