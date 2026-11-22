# B1L-0308 — Tampons de constantes Metal liés par adresse GPU (Mesa, set v147)

## Pourquoi

0307 : la fonction utilisateur la plus chaude de backboardd est le `memcpy` de `panfrost_emit_const_buf`
(~11 000 échantillons). `B2L UBO` : jusqu'à **618 Mo copiés par 5 s** (37 652 liaisons). Depuis B1L-0062, sur panfrost
(`ubo_base` 1), chaque tampon de constantes Metal était passé en `user_buffer`. `panfrost_map_constant_buffer_gpu`
(pan_cmdstream.c) recopie alors toute la plage liée dans le pool du lot, à chaque dessin.

La raison de 0062 (« le shader lit des zéros par adresse ») était fausse : B1L-0065 a trouvé la vraie cause, la
transformation viewport, pas la liaison.

## Changement

`air-b2l/apply.py`, bloc B1L-0308 :
- `air_metal.c` : un tampon dont le décalage est multiple de 16 (`constant_buffer_offset_alignment` de panfrost,
  pan_screen.c:652) est lié par ressource (`cb.buffer = b->res`, `buffer_offset` = décalage). La taille reste bornée à
  ce que les lectures atteignent (B1L-0115).
- Le cas non aligné reste en `user_buffer`.
- Panfrost lit alors l'UBO à son adresse, après `panfrost_batch_read_rsrc`. C'est le chemin que GLES prend pour tout
  UBO au-dessus de 0.
- Le descripteur v7 « Uniform Buffer » plafonne à 4096 entrées de 16 octets, et le pointeur est décalé de 4.
- Les mots poussés dans les uniforms (B1L-0063 pousse tous les UBO) sont toujours lus par le CPU, depuis
  `user_buffer + buffer_offset`. `user_buffer` vaut donc la projection du tampon (`b->map`), et l'assert de
  `pan_cmdstream.c` accepte une ressource accompagnée de sa projection.

## À vérifier

- `B2L UBO` : Ko copiés fortement en baisse.
- Profil : `panfrost_emit_const_buf` memcpy disparu.
- Rendu correct : couleurs, Liquid Glass, flous.
- i/s.
- Aucune faute Mali.

## Résultat

Flashé (BOOT `fe05d45b…`), testé avec 0309. `B2L UBO` : **618 Mo → 11–65 Mo copiés par 5 s**, rendu correct. i/s par 10 s : 18 à 41 (~23 en moyenne). Le propriétaire ne sent pas de grande différence ; ce premier boot après reconstruction de Mesa a recompilé les shaders (le premier a pris 39,6 s). Mesa draw : 1,2–3,3 s par 10 s.
