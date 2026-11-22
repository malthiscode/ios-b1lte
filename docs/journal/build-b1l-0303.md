# B1L-0303 — Mesure : où va le temps d'un tampon de commandes (Mesa + plug-in, set v144)

## Pourquoi

0302 : un tampon de commandes de backboardd (`-[HakiosMetalCommandBuffer commit]`) garde le verrou Mesa 30–35 ms en
moyenne : à 1–2 tampons par image, c'est déjà 15 à 30 i/s. Le reste (créations, appels au pilote ~25 µs) n'explique pas.

## Changement

- `hakiosmetal_air.m` : dans la boucle de `commit`, temps par sorte de travail (dessins, dont `render_draw` de Mesa seul ;
  effacements ; copies ; calculs ; `flush`) et durée totale par tampon. Ligne toutes les 10 s :
  `B2L COMMIT: <processus> 10 s: N buffers T ms (longest) | draws … (Mesa draw …) | clears | blits | dispatches | flush`.
- `hakios.drm.c` : `IOConnectMapMemory64` / `IOConnectUnmapMemory64` des tampons comptés dans `B2L RTT` comme 0x70 / 0x71
  (avec `kIOMapPrefault` depuis 0293, un gros tampon remappé coûte toutes ses pages) ; ligne coupée en deux au-delà de
  420 octets (`B2L RTT+`), le noyau en garde 511. Testé sur le Mac.
Mesure seule. Set v143 → v144.

## Résultat

Flashé (BOOT `65f3e9c0…`, BOOT-OK). Propriétaire : plus fluide au début, puis beaucoup plus lent au bout de peu de temps.

`B2L COMMIT` (backboardd, par 10 s en régime normal) : 100 à 300 tampons, **3 à 5,9 s de verrou**, dont **dessins 2,5 à 5,8 s
(`render_draw` de Mesa 2,1 à 5,3 s)** pour 10 000 à 28 000 dessins (~100 par image, 0,13 à 0,32 ms chacun) ; flush
0,1 à 0,7 s ; effacements, copies, calculs négligeables. Appels au pilote pendant ce temps ~1,4 s par 10 s (WAIT_BO
~90 et MMAP_BO ~70 par image). `B2L HOLD` : `newTextureWithDescriptor` ~1 s et `-[HakiosMetalTexture dealloc]` ~0,5 s
par 5 s en plus. Mémoire GPU de backboardd qui **grimpe : 344 → 630 → 978 → 1126 Mo** (dont 623 Mo dans le cache de
Mesa) — sans purge depuis 0302 ; c'est le ralentissement « au bout d'un moment ».
Démarrage : Mesa reconstruit → cache de shaders à refaire, 224 shaders compilés, **un seul a pris 48,7 s**
(allocation de registres Bifrost, `bi_allocate_registers`) en tenant le verrou Mesa : écran figé ~50 s au premier
démarrage après chaque build Mesa. Les lignes 0x70/0x71 de `B2L RTT` ont été coupées dans le log (mélange de lignes).
Profil processeur (adresses de libhakiosair, base 0x107200000) : `memset` de B1L-0185 dans `panfrost_bo_create` avec
défauts de page surtout au début ; la plus grosse part reste du temps noyau sans fonction visible → B1L-0304.
