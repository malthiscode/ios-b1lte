# B1L-0312 — Présentation des images par un fil du noyau (noyau, set v149)

## Pourquoi

0311 : chaque SwapEnd de backboardd (`fb 5`) dure ~7 ms (2 s par 10 s), parce que `AppleCLCD::swap` attend que le GPU
ait fini la surface de chaque couche (`HakiosMaliDrawn`, B1L-0096) avant de l'afficher. Le GPU n'est occupé que
28–41 % du temps : processeur et GPU s'attendent l'un l'autre à chaque image.

## Changement

`B2LFramebuffer.cpp` :
- SwapEnd copie l'enregistrement dans une file de 2 places et rend la main aussitôt.
- Un `thread_call` de priorité haute affiche les enregistrements dans l'ordre : attente du GPU, puis affichage.
- Une 3ᵉ image attend dans SwapEnd, comme avant. backboardd n'a donc jamais plus d'une image d'avance sur l'écran.
- Si la file ne peut pas être créée, l'affichage se fait par l'appelant, comme avant, avec une ligne dans le journal.

## À vérifier

- `fb 5` : quelques µs au lieu de ~7 ms.
- i/s en hausse ; latence au toucher.
- Pas de déchirure ni d'image qui saute.
- Démarrage normal.

## Résultat

Flashé (BOOT `4ea201f3…`). Démarrage normal, mais **défauts visibles** : blocs noirs et éléments qui clignotent,
écran pas dessiné d'un coup et décalé.

- `fb 5` : 4 à 276 ms par 10 s au lieu de 2 017 : SwapEnd ne bloque plus.
- **i/s inchangées** : 19 à 29, ~22 en moyenne.
- Lecture : quand SwapEnd rend la main avant que l'image soit à l'écran, CoreAnimation réutilise une surface encore
  balayée par l'écran (zero-copy), d'où les déchirures.
- Le gain est nul et le défaut visible : **annulé en 0313**. La version asynchrone est gardée dans
  `B2LFramebuffer.cpp.0312-async`.
- Le frein est donc le travail du fil d'affichage de CoreAnimation lui-même, pas l'attente du GPU.
