# B1L-0294 — Safari sans GPU de nouveau (set v140)

## Changement (un seul)

`b2l_metal_plugin.m` : `com.apple.WebKit.GPU` rejoint la liste des processus sans Mali. En 0292/0293 il avait le GPU
et les pages de Safari sortaient souvent blanches ; avant 0292, Safari dessinait sans GPU et marchait.
Logique retestée sur le Mac (`gate.c`, 12 cas). Set v139 → v140, bibliothèques reconstruites (sans Mesa).

## À vérifier

Safari affiche ses pages ; `b2l-plugin: com.apple.WebKit.GPU ... asks` absent du log ; « no memory ».

## Résultat

Flashé (BOOT `bf14e5f2…`, BOOT-OK), plug-in v140 avec WebKit.GPU dans la liste. Suivi tout de suite de 0295 (mesure seule) : le propriétaire teste les deux ensemble.
