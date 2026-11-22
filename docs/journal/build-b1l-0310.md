# B1L-0310 — Mesure des textures créées et détruites par backboardd (libhakiosmetal, set v148)

## Pourquoi

Boot 0309 (0308 + 0309) :
- `B2L UBO` : de 618 Mo à **11–65 Mo copiés par 5 s**. 0308 marche.
- **0 « no memory »**, donc le diagnostic 0309 n'a rien eu à dire.
- i/s par 10 s : 18 à 41, ~23 en moyenne. Le propriétaire ne sent pas de grande différence.
- Shaders : **le premier shader de backboardd a pris 39,6 s** à compiler, parce que reconstruire Mesa invalide le
  cache. Même chose en 0305 (48 s) et 0306 (36 s) ; 0307, sans reconstruction de Mesa, l'a trouvé en cache.
- `B2L HOLD` : **`newTextureWithDescriptor:` tient le verrou Mesa 1,3 s par 5 s, pour 1 721 textures**.
  `-[HakiosMetalTexture dealloc]` le tient 0,3–0,4 s pour ~2 000 textures. Les attentes de verrou montent à 1 s par 5 s.
- `B2L CREATE` ≥ 4 Mo : 32–38 tampons noyau par 10 s, 400–660 Mo, 0,2–0,4 s.
- `B2L TEX` montre des 1440×3040 RGBA16Float (35 Mo, les « tampons de 36 Mo ») et des formes de la taille des touches
  de la Calculatrice : 126×50 et 130×54, en BGRA8 et en RGBA16F. Le propriétaire voit ces touches apparaître une par une.

## Changement

`hakiosmetal_air.m` :
- Temps de `mesa.texture_create` et de `mesa.texture_destroy`, par taille en pixels : < 16 K, < 256 K, < 1 M, ≥ 1 M.
- Nombre de textures `MTLStorageModeMemoryless`.
- Durée de vie de chaque texture : < 50 ms, < 500 ms, plus.
- Une ligne `B2L TXL` toutes les 10 s.

Mesa n'est pas reconstruit, pour garder le cache de shaders.

## À vérifier

- Les textures vivent-elles moins d'une image (créées à chaque image par CoreAnimation) ?
- Lesquelles coûtent : les grandes, à cause de la remise à zéro B1L-0185 sur le CPU ?
- Y a-t-il des textures memoryless ? Sur Apple elles ne coûtent rien ; ici, une vraie allocation.

## Résultat

Flashé (BOOT `52892c85…`). Le cache de shaders a tenu : 629 trouvés, 21 compilés, le plus long 39 ms.

`B2L TXL` (backboardd, par 10 s, en usage) :
- **Textures créées : 1 000 à 3 300**, dont **776 à 1 557 memoryless**, environ la moitié.
- Durée de vie : la plupart moins de 50 ms ou moins de 500 ms.
- Coût de création : 0,6 à 1,4 s par 10 s ; une texture ≥ 1 M px coûte ~4 ms, une de < 1 M px ~2 ms.
- Coût de destruction : 0,2 à 0,5 s par 10 s.

i/s par 10 s : 17 à 30.

Conclusion : CoreAnimation crée ses textures intermédiaires (memoryless) à chaque passe. Sur Apple elles ne coûtent
rien ; ici chacune est une vraie ressource Gallium, faite puis défaite sous le verrou Mesa. → 0311.
