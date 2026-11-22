# B1L-0283 — identité Apple (ChipID/ECID) et Safari sans JIT

Base : 0282 (son et Bluetooth OK). Build fait par la session Wi‑Fi. Rien n'est écrit sur USERDATA.

## Pourquoi
D'après l'os_log du 0282 (image `out/live/ios26-userdata-net-282.img`, lue en lecture seule) :
- Bourse et Météo ont internet : leurs requêtes reçoivent des réponses 200.
- Les données de Météo (WeatherKit) répondent 401.
- akd : « Failed to get ChipID (0) or ECID (0) for secureDeviceId », pas de Stable-ID/SDID, anisette KO (ADIGetIDMSRouting -45059, AKAnisetteError -8025).
- Safari a été lancé en 0282, mais l'os_log persistant s'arrête environ 2 min après le boot : pas de trace de son plantage. Historique : le JIT de WebKit fige le téléphone dès son premier vidage de cache (0218–0223).

## Changements
1. `iokit/Kernel/arm/B2LPlatformExpert.cpp` : `/chosen chip-id` = 0x8140 (t8140, D94) et `unique-chip-id` = ECID synthétique 0x0002423240100283 (jamais celui du téléphone). Boot-arg `b2l_chip_id` : 0 coupé, 1 tous les processus, 2 (défaut) démons d'identité seulement.
2. `iokit/Kernel/IOUserClient.cpp` (`b2l_hide_product_id`) : ces deux propriétés ne sont lues que par akd, adid, appleaccountd, fairplaydeviceidentityd et mobileactivationd. Les autres processus ne les voient pas, pour ne pas changer l'UDID de MobileGestalt ni rejouer l'arrêt de backboardd du 0275.
3. `bsd/kern/kern_mman.c` (patch `patches/0109-b2l-webkit-no-jit.patch`, écrit en 0224 mais jamais appliqué avant) : MAP_JIT refusé (EPERM) aux processus `com.apple.WebKit*`, donc JavaScriptCore tourne sans JIT. Boot-arg `b2l_wk_jit=1` pour rendre le JIT.

## À vérifier
- Journal noyau : `B2L: /chosen chip-id/unique-chip-id synthetic, identity daemons only` et `B2L: JIT mapping refused for com.apple.WebKit.WebContent (B1L-0283)`.
- Météo : des données s'affichent-elles ? os_log d'akd : plus de « Failed to get ChipID » ? anisette ?
- Safari : une page s'ouvre-t-elle sans figer le téléphone ? L'ouvrir dans la première minute après le démarrage, pour que l'os_log la garde.
- Le son, le Bluetooth, l'écran et le Wi‑Fi doivent marcher comme en 0282.

## Résultat
- Test du propriétaire : Safari ne fige plus le téléphone et charge des pages, mais certaines sont mal affichées. Les vidéos YouTube et Reddit ne se lancent pas, et les téléchargements ne marchent pas. Fichiers n'affiche qu'une page blanche. Bourse ne charge pas. Météo ne trouve aucune ville en recherche. News marche.
- Journal noyau `out/logs/0283-boot.txt` : JIT refusé à WebKit (B1L-0283), aucun B2L CRASH.
- USERDATA copié en lecture seule dans `out/live/ios26-userdata-net-283.img` (copie en cours au moment de la note), pas encore analysé.
- Idée du propriétaire à évaluer : ajouter un numéro de série et un numéro de modèle synthétiques.

## Analyse de l'os_log 0283 (démarrage 07:42, lecture seule)
- ChipID/ECID : akd ne dit plus « Failed to get ChipID (0) ». L'enregistrement anisette démarre chez Apple (« Start provisioning response code: 200 », « ADIProvisioningStart succeeded ») mais échoue à la fin (« ADIProvisioningEnd failed -43004 »). Avant : SecKeyCreateRandomKey -26276 (pas de clé matérielle SEP) et DeviceIdentity « Failed to create reference key » (MobileActivation).
- Météo : 15 réponses 401. Chaque signature AMS échoue (« AMSMescal: Failed to mescal-sign », AMSFairPlayGetHardwareInfo -42184) parce que launchd ne trouve pas fairplayd (désactivé dans disabled.plist par le USERDATA propre de 0250). La recherche de ville passe aussi par geod, qui donne « Unable to load active tile group » (fichier absent, code 260) alors que son manifeste répond 304.
- Bourse : 3 réponses 200, puis CloudKit refuse la recherche d'achats (CKErrorDomain 2).
- Vidéos YouTube et Reddit : aucun décodeur vidéo matériel. AppleAVD, H264H8 et ProRes ne trouvent pas leur matériel, ce qui est normal sur l'Exynos sans pilote MFC.
- Fichiers et téléchargements : rien de clair dans l'os_log, et aucun plantage dans le journal noyau.
- Numéro de série et de modèle : aucune propriété `serial-number` / `model-number` publiée, et aucun processus ne se plaint de leur absence dans l'os_log. Ils ne débloqueront pas Météo ni Bourse à eux seuls.
