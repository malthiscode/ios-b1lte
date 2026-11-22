# B1L-0323 — Adresses GPU : petits tampons sous 1 Go, gros au-dessus (noyau, set v152)

## Pourquoi

0322 : l'application Réglages plante (SIGABRT, 7 fois), surtout quand on change un réglage.
- Son ouverture du GPU échoue : `HakiosMali: a buffer of 134217728 bytes cannot be entered into the GPU's address
  space`. C'est le tas du tiler (128 Mo d'adresses).
- Mesa ne fait alors pas d'écran panfrost, Metal n'a pas de périphérique, puis abort.
- Les mêmes refus touchent des cibles de 36 et 96 Mo dans backboardd (blocs noirs, fautes GPU à 0).

Cause : `mali_va_alloc` (`hakios.mali_va.h`) donne les adresses GPU (32 Mo–4 Go, une seule plage pour tous les
programmes) en parcourant la plage à la suite, en tournant. Les petits tampons finissent semés sur les 4 Go et il ne
reste plus de plage libre de 128 Mo d'un seul tenant, alors que ~1,2 Go seulement sont pris.

## Changement

`xnu-overlay/bsd/dev/hakios.mali_va.h` :
- Tampons **< 1 Mo** entre 32 Mo et 1 Go ; tampons **≥ 1 Mo** entre 1 Go et 4 Go. Chaque zone a son propre parcours
  (`next`, `next_big`).
- Si une zone est pleine, on cherche dans toute la plage, comme avant.
- Les règles d'origine restent : sous 4 Go, le code ne traverse pas de ligne de 16 Mo, un granule vide après chaque
  tampon.

Test : `tools/mali_va_test.c` (`cc -I xnu-overlay/bsd/dev tools/mali_va_test.c`) → ok. Il vérifie les zones, un tas de
128 Mo après 200 000 petits tampons et ~1 Go de cibles, les lignes de 16 Mo et le débordement d'une zone pleine.

## À vérifier

- Plus de `cannot be entered into the GPU's address space` ; Réglages ne plante plus.
- Moins de fautes GPU et de blocs noirs.

## Résultat

Flashé (BOOT `b4bb6dfa…`, BOOT-OK). Testé avec 0324/0325 par-dessus.
