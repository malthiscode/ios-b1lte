# B1L-0315 — Cibles de rendu recyclées, vidées par le GPU au premier chargement (libhakiosmetal, set v150)

## Pourquoi

0310/0311 :
- Une texture ordinaire de ≥ 1 M px coûte 5 à 10 ms à créer. C'est la remise à zéro B1L-0185 sur le processeur, plus
  la création noyau.
- Seules les textures memoryless étaient recyclées (0311).

## Changement

`hakiosmetal_air.m` :
- Les cibles de rendu ordinaires à un niveau et un échantillon (usage RenderTarget) entrent aussi dans le pool de 0311.
  Le pool passe à 128 Mo, et une texture n'y reste plus que 1 s.
- Une cible reprise du pool est marquée « périmée » : elle contient encore les pixels d'une autre.
  - La première passe qui la **charge** (`MTLLoadActionLoad`) la vide à zéro, comme une texture neuve de B1L-0185.
    C'est le GPU qui fait ce travail, pendant la passe.
  - Pareil pour la profondeur et le stencil.
- Une passe qui vide ou ignore le contenu, un `replaceRegion` ou une copie vers la texture retire la marque.
- `B2L TXL` dit aussi combien de cibles périmées ont été vidées.

Contient aussi 0314 (cœurs par fil).

## À vérifier

- `reused` en hausse, coût de création en baisse.
- Aucun reste d'image ni fond noir inattendu.
- `B2L PROF T` du fil `com.apple.coreanimation.display` : sur quels cœurs.

## Résultat

Flashé (BOOT `e5eb3296…`). Pas de rendu cassé, mais **le système s'est figé** à T+80 s, sans repartir. Le log
s'arrête sur `B1L-0258: 64 purgeable buffers taken back … available 3482 MB`, c'est-à-dire la reprise qui suit l'échec
d'une création de tampon. Le log 0311 s'arrêtait aussi sur « no memory for a buffer of 36175872 », sans la ligne du
diagnostic 0309 qui suit. **Le diagnostic 0309 (KMA_VAONLY + kmem_free) est le suspect du gel** : retiré en 0316.

- **Cœurs (0314)** : le fil `com.apple.coreanimation.display` passe 55–68 % de ses échantillons sur les M4, 20–30 %
  sur les A75 et 10–15 % sur les A55.
- **Thermique** : la grappe M4 est à 64–72 °C. Avec nos seuils (65 / 60 / 72 °C), **le M4 reste au plancher de
  936 MHz presque tout le temps**, au lieu de 2,08 GHz. Pareil en 0311. A55 et A75 descendent aussi à ~950 MHz.
- **Textures** : memoryless 511–1 054 par 10 s, reused 249–692. Aucune cible périmée n'a été chargée : CoreAnimation
  vide ou ignore toujours. Créer une texture < 1 M px coûte encore ~1,5 ms, avec le processeur bridé.
- i/s par 10 s : 10 à 45.
