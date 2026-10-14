# Combat

Ce document suit le jalon 4. Code : `src/engine/world/combat_control.*`, `remap_action` dans `src/engine/world/actor_anim.*`, tables d'armes dans `src/core/dtable.*`. Noms : `analysis/names_manual.tsv`.

## Le Controller (`CRUSADER.C`, segment `1138`)

Le joueur est piloté par un processus « Controller » (type 0x247, cadencé à 60 Hz, actif à tous les niveaux, abonné au clavier ; table virtuelle `DS:296B`). Sa méthode `+0x28` (`1138:220A`) répartit les messages selon la table `CS:2734` :

| Message | Traitement |
|---|---|
| 1 | `DS:2875` = 0 |
| 2 (minuterie) | un tir de rafale : `DS:2857` = 1, tir avec `explicit` = 1, puis `DS:2857` = 0 |
| 4 et 0x100 (réveil) | « tir en cours » (`DS:2856`) = 0 |
| 0x10 (clavier) | voir ci-dessous |

**Clavier** : codes de triche (`1138:2B03`), puis, si le jeu tourne, que le PNJ contrôlé est vivant et que ce n'est pas le joueur en stase (`DS:7E10`) :
1. pendant un tir en cours, un appui sur Espace ou sur Inser du pavé numérique (0x52 sans préfixe E0) est ignoré ;
2. le contrôle des déplacements (`1138:12CF`, `AvatarMover` dans le portage) reçoit l'événement ;
3. s'il ne l'a pas pris et que c'est un appui :
   - Espace / Inser du pavé → tir (ci-dessous) ;
   - au premier appui seulement :
     - `*` du pavé → arme suivante (`13E8:0C72`) ;
     - `−` du pavé → objet suivant (`13E8:0DC0`) ;
     - Suppr du pavé → un « u » envoyé à la vue du jeu (utiliser l'objet actif, voir « Commandes du joueur en jeu » dans `docs/interface.md`) ;
     - `+` du pavé → un « s » (mode de sélection, non reproduit) ;
   - Tab et J, à chaque répétition → **saut**, si le PNJ contrôlé est le joueur (`1138:253F`, `jump` dans `player_commands.cpp`) :
     - sans animation en cours : dernière action de repos (`1138:0014`) → 0x3A, saut sur place vers l'avant (élan de 3 images, puis environ 110 unités en avant et 24 de haut) ; en course (dernière action 0x38, ou `Actor_is_running` : 3, 0x30, 0x31, 0x38) → 0x38, le même saut sans l'élan ; sinon l'arme est rangée (0xB, refusée si occupé) et 0x3A suit (au plus une animation en attente) ;
     - avec une animation en cours : seulement si c'est la seule (`Kernel_I_getNumProcesses(PNJ, 0xF0)` = 1), que le joueur court et qu'elle n'est pas déjà 0x38 : elle échoue (fonction virtuelle `+0x0C`) et 0x38 part ;
     - vérifié (`game_tests`) : Tab depuis la garde, sur `SAVEGAME.13`, range l'arme puis saute de 160 unités ; J en pleine course donne 0x38 ;
4. sinon (relâchement, ou événement pris par les déplacements), Espace / Inser du pavé appellent le tir avec `explicit` = 0.

Rien de tout cela sur la carte 40 (`NPC_10f0_0122`).

**Appui sur une touche de tir** : au premier appui (compteur 1), l'heure est notée et « tir depuis l'appui » (`DS:287A`) remis à 0. Si le PNJ n'est pas occupé : en position de combat (`NPC_10f0_1f40` : dernière action autre que 0, 1, 0x3A, 0xB, 0x10), tir avec `explicit` = 1 ; sinon il lève son arme (action 7).

## Le tir (`CRUSADER_1138_2789`)

`DS:2854` dit qu'un tir est demandé. À la fin de chaque appel il prend la valeur de `explicit` (hors rafale) : **le premier appui ne fait qu'armer la demande**. Le tir part à l'appel suivant : une répétition du KeyDaemon si la touche reste enfoncée (au bout de 10 ticks), ou le relâchement s'il n'y a pas eu de tir depuis l'appui. Une pression brève tire donc une fois, au relâchement.

Conditions : pas de rafale en cours (`DS:2858` ≠ 0 hors message 2) ; pas occupé si `explicit`. Puis, avec une arme active et une demande en cours (et, si `explicit` = 0, aucun tir depuis l'appui) :

1. **Plus de munitions** (joueur) : si un chargeur de la bonne munition est dans l'équipement, rechargement (`NPC_10f0_3ba3`) après remise à zéro de l'état de tir (`2fd4`), puis attente de 15 ticks ; sinon clic (son 0x2A), demande annulée, attente de 20 ticks.
2. **Fusil à pompe** (forme 0x332 = 818), joueur debout, pas encore armé (`DS:2879`) : son de l'arme, action 0xA (remplaçant les animations en cours), armé.
3. **Tir** : action 8 (0x2A à genoux), une munition consommée (`NPC_10f0_3ab2`), alerte des PNJ visibles (`CRUSADER_1138_2daf`, au plus toutes les 240 ticks), attente du délai de l'arme. Une rafale de n > 1 coups arme n − 1 tirs supplémentaires, envoyés par la minuterie (message 2).

« Attente » est un `Process_sleep` du Controller ; le réveil (message 4) termine le tir en cours.

**Munitions** (`NPC_10f0_3a1f`) : qualité de l'arme pour une arme à munitions ; énergie / coût par tir pour une arme à énergie ; 1 (illimité) pour une arme sans l'un ni l'autre, comme le pistolet de départ (forme 814). Un tir (`3ab2`) retire 1 à la qualité, ou le coût à l'énergie (pas en dessous de 0, `NPC_10f0_379a`). Rechargement (`3ba3`) : qualité de l'arme = taille du chargeur, nombre de chargeurs − 1, chargeur détruit à 0 ; son de rechargement ; action 0xA si le joueur est debout.

## Tables d'armes (`DTABLE.C`)

Enregistrement de 0x12 octets en `DS:215A` (21 entrées) : +0 forme, +2 ?, +4 délai entre deux tirs (ticks), +6 type de munitions, +8 coups par rafale, +0xA coût en énergie par tir (octet), +0xC « actions conservées » (octet, voir plus bas), +0xD icône (mot non aligné), +0xF son de rechargement (octet), +0x10 ?.

Munitions en `DS:20D6` (8 octets par type) : +0 forme, +2 taille du chargeur. Types 1 à 5 : formes 829 à 833, chargeurs de 20, 12, 60, 8 et 10.

Arme superposée (`WPNCACHE_1160_0771`) : `DS:2B82` (3 octets : forme d'arme, classe) donne la classe, `DS:2B64` la forme : 0x1A1, 0x36D, 0x36F, 0x36E, 0x33B (« Small Pist », « Large Pist », « Small Rifl », « Large Rifl »…).

## Actions selon l'arme (`CRUSADER_1130_2105`)

Appliqué au début d'`Actor_I_doAnim` :
- un PNJ autre que le joueur garde les actions 4, 0xB, 7, 8, 0x24 et 2 (`DS:279B`), et sa course armée 0x30 devient 3 ; les autres actions suivent la règle du joueur ;
- sur la carte 40, les six premières paires de `DS:2747` s'appliquent, arme ou non (la boucle s'arrête sur la longueur de `DS:277F` mais lit `DS:2747`) ;
- sinon, avec une arme dont l'octet +0xC vaut 0, les paires de `DS:2747` : 4→0x25, 0xB→0x10, 7→0xC, 0x2E→0x2F, 0xA→0xF, 0x30→0x31, 3→0x31, 0x22→0x23, 0x27→0x19, 0x24→0x2C, 2→0x2D ; les tirs 8 et 0x2A deviennent 0xD et 0x2B, ou 0x36 et 0x32 pour le joueur quand l'arme superposée est 0x36E ou 0x33B.

## L'image de tir

Le processus d'animation (`ANIMPRIM`) appelle `WPNCACHE_1160_005f` sur une image dont les décalages de la bouche du canon (octets 5 à 7, voir `docs/animation.md`) ne sont pas tous nuls. Celle-ci appelle `Item_I_fireWeapon` (`1130:11DA`) avec le type de tir de l'arme (`DTABLE_1120_0528`), puis joue le son de l'arme (`DTABLE_1120_01cf`). Exemple : action 8 du joueur, image 0 seulement.

`Item_I_fireWeapon(objet, dx, dy, dz, direction, type de tir, viser)` est décrit ci-dessous. `WPNCACHE_1160_005f` lui passe les décalages de l'image multipliés par 16 (x, y) et par 8 (z) : chaque direction a ses propres images, les décalages sont donc déjà orientés.

## Types de tir (`FIRETYPE.C`, table `DS:25B4`)

16 enregistrements de 0x11 octets (`DTABLE_1120_062d` en trouve un par numéro) : +0 numéro, +2 / +4 dégâts minimum et maximum (`DTABLE_1120_0674` tire au hasard entre les deux), +6 rayon de l'effet de zone, +7 projectiles par tir, +0xB pas de dispersion, +0xC distance par pas (en unités de 16), +0xE portée (en pas), +0x10 sprite d'impact.

| Type | Dégâts | Projectiles | Armes (forme) |
|---|---|---|---|
| 1 | 18–25 | 1 | 819, 820 |
| 2 | 10–17 | 8 | 818 (fusil à pompe) |
| 3 | 200 (zone 10) | 1 | 904 |
| 5 | 25–45 | 1 | 816, 906 |
| 6 | 15–35 | 1 | 909 |
| 10 | 45–80 (zone 10) | 3 | 910 |
| 11 | 14–20 | 1 | 814, 815 (pistolets) |
| 13 | 10–17 | 5 | 907 |
| 14 | 250 (zone 4) | 1 | 902 |
| 15 | 35–55 (zone 4) | 1 | 908 |

Les types 4, 7, 8, 9 et 12 servent à d'autres tireurs (PNJ, pièges, objets). Le type de tir d'une arme est le mot +0x10 de son enregistrement, son son le mot +2.

## Le tir (`Item_I_fireWeapon`, `1130:11DA`)

1. Départ : position du tireur (celle de son conteneur le plus extérieur, `ITEM_10a8_03fa`) plus le décalage de la bouche du canon.
2. Dégâts du tir : tirés une fois, dans la plage du type de tir de l'arme du tireur s'il s'agit d'un PNJ (`NPC_10f0_3106`), du type de tir donné sinon.
3. Si le point de départ est déjà dans un objet (place libre avec la forme 0x1D9, `ITEMFIND_10e8_1874`, voir `docs/animation.md`), le premier objet solide trouvé est touché aussitôt. Le tireur n'est pas mis de côté : les vraies images de tir placent la bouche du canon hors de sa boîte (joueur vers le nord : (−16, −48, 56)), et les tests du portage tirent depuis ces décalages.
4. Sinon, autant de projectiles que le type l'indique, image de sprite selon le type (0 ; direction + 0x11 pour 3, 9, 10 ; direction + 1 pour 5 ; 0x46 pour 6 ; 0x47 ou 0x48 pour 14 ; 0x4C pour 15).
5. Visée, quand elle est demandée : pour le PNJ contrôlé, `TRACKER_1128_0158` cherche une cible dans la direction du tir (voir plus bas) ; pour un autre tireur, la cible est le PNJ contrôlé. Avec une cible, le projectile vise son point de visée (`CRUSADER_1130_00ee`, ci-dessous) ; sans cible, un point à 0x500 pas de direction droit devant (`SPRITE_1140_0e01`, tables `DS:132F` / `DS:1340`, identiques aux directions du portage). Un tir du PNJ contrôlé le fait d'abord entrer en mode de visée ; dans ce mode, sans cible, le projectile vise le réticule (`SPRITE_1140_0e88`, voir « Mode de visée » plus bas), toujours avec dispersion.
6. Dispersion (`SPRITE_1140_09c9`), quand le type le permet (+0xB nul) : toujours vers une cible, et droit devant seulement si la visée est demandée. Écart = distance de Tchebychev au point visé / k, au plus 80 ; x et y bougent de ± écart, z de ± écart / 3 (au plus 24) et reste entre 0 et 250. Le diviseur k :
   - tir du PNJ contrôlé : 8 (types 2, 13), 12 (5), 5 (10), 10 sinon ;
   - tir d'un PNJ : 2 si l'octet +0x68 de sa fiche est non nul ; 8 si le PNJ contrôlé n'a pas bougé récemment (`CRUSADER_1130_1180`) ; sinon le diviseur de mouvement `DS:2877` (2 ou 3) ;
   - autre tireur (tourelle…) : 5 si le PNJ contrôlé est à genoux, sinon 3 × `DS:2877`.

   « Bouger récemment » : quand le PNJ contrôlé commence certaines actions (`Actor_I_doAnim`), l'heure est notée (`DS:2869`) avec une durée (`DS:286D`) et un diviseur (`DS:2877`, 1 au départ) : actions 1 et 2 → 120 ticks, 3 ; 0x1C, 0x1D → 60, 3 ; 0x28, 0x29 → 75, 3 ; 3, 0x17, 0x18, 0x19, 0x22, 0x23, 0x26, 0x27, 0x38, 0x3A, 0x3D, 0x3E → 120, 2. Plus le joueur bouge, plus les ennemis le ratent.

   Pour les types 2 et 13, `09c9` décale encore le point visé de (distance / 16) × 16 / 5 en x et y et × 8 / 5 en z, mais après avoir calculé les pas : le vol en ligne droite ne s'en sert pas.

### Les cibles suivies (`TRACKER.C`, segment `1128`)

Une liste de 100 emplacements (`DS:26E0`, nombre d'objets en `DS:26E6`) tient les objets qu'un tir peut viser : formes dont l'octet 0 ou l'octet 6 de `TYPEFLAG.DAT` a le bit 4 (`ITEM_10a8_30a8` / `30d2`), non cassées (statut 0x8000). Un objet prend le premier emplacement libre (`1128:0098`) en entrant dans la zone active, quand il est réparé (`ITEM_10a8_260f`) ou quand il reçoit une telle forme (`Item_I_setShape`) ; il libère le premier emplacement qui le contient (`1128:00fe`) en quittant la zone active, en cassant (`Item_I_setBroken`), en étant détruit (sauf le joueur) ou en perdant la forme. `setShape` ne regarde pas la zone active. La liste est vidée au chargement et au changement de carte. Sur `SAVEGAME.13`, 23 objets y sont.

**Recherche de cible** (`TRACKER_1128_0158`, tireur, direction, 16 directions ?) : le dernier argument vient du bit 6 de l'octet 3 de la dernière action du tireur (les tirs du joueur sont en 16 directions). Les emplacements sont parcourus dans l'ordre ; un objet est candidat s'il n'est pas le tireur et qu'il est en partie à l'écran (`Item_I_isPartlyOnScreen`). Sa direction depuis le tireur (coin des objets) doit être la direction du tir : `Item_I_getDirFromTo16` en 16 directions ; en 8 directions, la direction paire de `ITEMFIND_10d8_0120` est ramenée entre 0 et 7 par `getClosestDirectionInRange` puis doublée, ce qui ne donne juste que vers 0 (bizarrerie de l'original, conservée). Préférences : dès qu'un PNJ est retenu, les autres objets sont ignorés ; de même, dès qu'une forme à l'octet 0 bit 4 est retenue, celles qui ne l'ont pas sont ignorées ; entre candidats de même rang, le plus proche (distance de Tchebychev, z compris). Une cible à plus de 0x400 est abandonnée.

**Point visé** (`CRUSADER_1130_00ee`) : centre de l'empreinte en x et y (`getCX` / `getCY`). En z, avec h la hauteur de l'empreinte : z + 8h, moins 8 si h vaut 1 ou 2 ; si h ≥ 3, moins 16, puis + 8 si le tireur est au moins 48 plus bas, ou, s'il est au moins 48 plus haut, − 16 (h = 6) ou − 24 (h ≥ 7). Pour un PNJ (h = 10) : z + 64.

## Mode de visée et désignation de cible (`1138:0299` à `087F`, `10C8`)

Le **mode de visée** (`DS:2863`) est celui de la position de combat du PNJ contrôlé : un petit réticule rouge (forme 0x4CC) devant lui, que les flèches font tourner.

- **Entrée** (`1138:0299`) : à chaque `Actor_I_doAnim` du PNJ contrôlé (ou d'une forme 0x580) avec une action « de visée » : 2, 4, 5, 6, 7, 8, 0xC, 0xD, 0xF, 0x24, 0x25, 0x28 à 0x2F, 0x32, 0x36 (après le remplacement selon l'arme) ; et avant chaque tir du PNJ contrôlé (`Item_I_fireWeapon`). Refusée pendant une animation « de rotation » (compteur `DS:288A`) et pour la forme 0x576. Si le mode est déjà actif, elle marque seulement le réticule à replacer (`DS:2862`), ce que fait le `Controller` à son prochain passage (`1138:087F`, sa méthode `run`, appelée à chaque tranche quand il ne dort pas). Un PNJ dont la forme n'a pas d'action 1 (une caméra de surveillance : `DS:2864`, noté au changement de PNJ contrôlé) n'y entre jamais.
- **Sortie** (`1138:0692`) : toute autre action du PNJ contrôlé la quitte, et remet à zéro le tir du `Controller` (`1138:2F9F` : rien de voulu ni en cours, pas de rafale, minuterie arrêtée, fin de l'`AutoFirer`). Le réticule disparaît, l'angle repasse à 1000, la marque de cible aussi.
- **Rotations** : les actions 0x17, 0x18, 0x19, 0x1C, 0x1D, 0x27, 0x3D, 0x3E (roulades, pas de côté, fin de course), ainsi que le pivot de la position de combat par les touches 4 / 6 du pavé et les directions absolues, portent un drapeau (`1158:2E55`, +0x40, `DS:288A` + 1) : le mode est quitté et ne revient qu'à la fin de l'animation (`1158:2E66`).

**Le réticule** (`1138:0708`) est un sprite d'écran (gestionnaire `SPMGR`, 64 emplacements dessinés par-dessus la scène). Il est posé à 200 unités du centre du PNJ (`getCX`, `getCY`), à l'angle `DS:285E` en degrés : x + 200 cos a, y + 200 sin a, avec les tables de `STATIC/TRIG.DAT` (256 valeurs en 16.16 par tour, index = (a mod 360) × 1000 / 1406, au plus 255 ; partie entière du produit). Sa hauteur est le z du PNJ plus la hauteur de la bouche du canon dans la première image de tir de son action de tir (8, 0x2A à genoux, remplacées selon l'arme ; `1150:065D`). Un angle supérieur à 360 (1000 à la sortie du mode et au changement de PNJ contrôlé) est d'abord tiré de la direction d : (22 d + 5 + 270) mod 360, partie entière de 0x160005 × d + 0x50006 en 16.16 (22 degrés par direction au lieu de 22,5). Pour la direction 4 (+x) : 3 degrés, soit l'entrée 2 des tables, et le réticule à (+199, +9) du centre.

**Tourner** (`1138:10C8`, depuis l'`AvatarMover` en position de combat : flèches gauche / droite, ou celles du pavé avec Verr Num) : l'angle bouge de 5 degrés, 2 sans Maj (marche seule) ; une rotation envoyée par la souris (`DS:5FF6`) tourne de la moitié de sa cadence (`DS:5FF8`), 20 degrés au plus (voir `docs/interface.md`, « Souris en jeu ») ; la manette n'est pas reproduite. Le réticule est replacé, et le PNJ prend la direction du réticule vu de son centre (`Item_I_getDirFromTo16`) : position de combat (4, ou 0x2E à genoux) dans cette direction quand elle change. Hors du mode, un PNJ sans action 1 pivote d'une direction par appui s'il n'est pas occupé.

**Désignation de cible** (`DS:2889`, basculée par R, active au lancement) : dans le mode, la cible trouvée dans la direction du PNJ (`TRACKER_1128_0158` en 16 directions) reçoit une marque si sa forme a le bit 4 de l'octet 0 de `TYPEFLAG` (`ITEM_10a8_30a8`).

- `1138:0459` (désigner) efface la marque précédente, puis n'en pose une que si la direction du PNJ diffère de celle de la dernière désignation (`DS:2890`, remise à 0x10 en désignant « rien ») et qu'une ligne de tir existe (`Item_I_fireDistance`). Bizarrerie conservée : une nouvelle cible qui entre dans la ligne de mire sans que le PNJ tourne n'est pas marquée, et un nouvel essai « rien » sur une désignation déjà vide ne remet pas `DS:2890` à 0x10.
- La marque est un `SpriteProcess` de forme 0x59A (un cercle qui se resserre), images 0 à 5, 10 ticks chacune, qui reste ensuite sur l'image 5 (`1140:0444` garde l'emplacement `DS:288C` au lieu de le libérer). Elle est posée en (x, y, z) de la cible : z + 8 h', h' étant la hauteur de l'empreinte h ramenée à h / 2 + 1 (h ≥ 4) ou h / 2 (h ≥ 2), et, hors PNJ, diminuée de 2 (1 donne 0) ; puis reculée de 16 × (n / 2) le long du plus grand côté n de l'empreinte (rien si elle est carrée). Pour un PNJ (3 × 3 × 10) : (x, y, z + 48).
- La cible est recherchée à nouveau : à l'entrée dans le mode (`1138:03D0`) ; en tournant ; au plus une fois par seconde (`1138:02F3`, appelée par le processus `DumbTimer` de `UNITFILE.C`, créé au démarrage, de tout niveau, à chaque passage où plus de 60 ticks se sont écoulés) ; quand la cible bouge (`Item_I_move`, appelée par les animations et la chute : la marque la suit tant qu'elle reste la cible trouvée) ; quand elle est détruite (`10B0:1AB3`). La mort du PNJ contrôlé efface la marque.

Dans le portage : `aim_mode.hpp` (état dans `GameState::Aim`), les sprites d'écran dans `screen_sprites.hpp` (seuls le réticule et la marque y passent ; les impacts restent des objets « sprite seulement », et le cadre de sélection est dessiné à part), les tables dans `core/trig.hpp`. Vérifié (`game_tests`, `SAVEGAME.13`) : la position de combat fait apparaître le réticule à (+199, +9), douze appuis sur la flèche droite le tournent de 24 degrés et le joueur passe de la direction 4 à 5 ; un PNJ placé dans cette direction est marqué en moins d'une seconde à (x, y, z + 48), la marque finit sur l'image 5, suit le PNJ déplacé et disparaît avec lui ; un tir sans cible part vers le réticule ; la marche quitte le mode. Capture `--fire` : le réticule est dessiné devant le joueur, par-dessus le décor.

Non reproduits : la sauvegarde de l'état des sprites d'écran (entrées 30 à 32 des parties), la translucidité des sprites d'écran, l'entrée dans le mode au démarrage particulier d'une animation (`ANIMPRIM_1158_016f`, cas non portés), l'`AutoFirer` (type 0x260, arme 0x38D).

## Le projectile (« SuperSprite », `SPRITE.C`, type 0x250)

n = (distance de Tchebychev / 16) / vitesse (+1 si la distance / 16 n'est pas un multiple de 16 ou est nulle). Toutes les 2 ticks (3 pour le type 9), le pas i le place en départ + (cible − départ) × i / n. Chaque pas est balayé depuis la position précédente avec la forme 0x1D9 :
- balayage libre : le projectile avance (sprite déplacé ; traînée pour le type 3) ;
- bloqué, à z 0 ou 250, ou au-delà de la portée (+0xE, plus 25 pas) : impact. Pendant les 5 premiers pas, le tireur lui-même est mis de côté et le pas rejoué. À l'impact (`SPRITE_1140_1098`), le point exact est cherché par pas de 16 unités, l'objet touché reçoit les dégâts avec la direction du projectile vers lui (`ITEMFIND_10d8_0120`), puis effet de zone (`TRACKER_1128_039c`), sprite d'impact (`SPRITE_1150_02df`) et fin du processus.

**Guidage** (type 9, `1140:151D`) : au lancement, vitesse = pas de la direction × 16. Tant que le nombre de pas reste sous la portée (+0xE), la vitesse tourne vers la cible (l'objet visé, sinon la direction de lancement) : on ajoute le pas de la direction du missile vers elle, doublé sur l'axe où il va dans l'autre sens ; en z, ±1 vers le centre de la cible + 8. Au-delà de la portée le missile est perdu et chute (vz − 1 par pas). Vitesse bornée à ±32 en x et y, ±16 en z ; l'image suit la direction (direction + 0x11) ; un pas toutes les 3 ticks.

**Ricochet** (type 5) : bloqué par un objet dont l'octet 1 de `TYPEFLAG` a le bit 2, et pas plus de 5 fois, le projectile ne s'arrête pas : sprite d'impact, puis la composante x de son vol est inversée s'il arrive par une face x de l'objet (hors de son empreinte en x), sinon la composante y ; il repart de là, à plat, sans dispersion (image direction + 1).

## Les dégâts (`CRUSADER_1130_026b`)

1. Difficulté (`DS:7E19`, mot +0xB de l'entrée 20 des sauvegardes, 1 dans toutes celles du jeu) : au niveau 1, les dégâts reçus par les autres que le joueur sont multipliés par 5, ceux reçus par le joueur divisés par 5 ; au niveau 2, par 3.
2. Les robots (formes 0x4C8, 0x338, 0x45D, 0x2CB, 0x4E6, 0x383, 0x385, `CRUSADER_1130_01f7`) ne prennent qu'un tiers des types 1, 2, 11 et 13 (balles).
3. Résultat ramené entre 1 et 250.
4. Objet qui n'est pas un PNJ : voir « Objets touchés » ci-dessous. Ensuite, les explosifs (types 3 et 4) lancent un objet qui a un poids (`TYPEFLAG` octet 7) dans la direction du coup : vitesse (`DS:26F8`[dir] × 10–25, `DS:2718`[dir] × 10–25, 0), gravité 2 (`Item_I_hurl`).
5. Vargas (forme 0x3AC) tant que son bouclier (`DS:2895`, 5 000 au départ) n'est pas vide : le coup est sans effet pendant que son processus d'animation montre l'action 0x21 ou 0x20 ; sinon le bouclier s'allume (action 0x21 qui remplace les animations, puis 0x20) et perd les dégâts (pas sous 0), sans toucher aux points de vie. `World::I_resetVargasShield` (0x099, classe `EVENT`) le remet à 500 s'il n'est pas vide. Le portage garde la valeur dans l'état du jeu, remise à 5 000 au chargement d'une sauvegarde (dans l'original, une variable du segment de données, non sauvegardée).
6. PNJ vivant : heure du coup notée (+0x69). S'il n'est ni le joueur ni le PNJ contrôlé, il riposte : déjà en combat, il reprend l'activité 5 s'il était en activité 8, et vise le PNJ contrôlé ; sinon il prend son activité par défaut 2 (`Actor_I_setActivity(0x72)`) et, si c'est une activité de combat, vise le PNJ contrôlé. Touché par le PNJ contrôlé à 0x280 au plus, à l'écran, avec la fiche +0x59 bit 5 et une ligne de tir vers lui, son processus de marche (type 0x204) s'arrête. Les tirs électriques (types 7 et 15) qui ne tuent pas secouent le joueur (ses animations coupées, action 0x37) et, une fois sur trois, font combattre 8 à 17 secondes les robots 0x4E6, 0x338, 0x385, 0x383. Puis boucliers du joueur (fiche +0x66 : 1, 2, ou 3 compté comme 4) : si le masque du type de tir (+0xA) contient ce bit et que l'énergie couvre son coût (+8), l'énergie paie et le tir ne fait rien (son 0x48 ; éclat de la barre d'état et sprite de bouclier 0x5A9 / 0x52B non reproduits) ; un PNJ immortel (+0x13 bit 1) ne perd rien ; points de vie − dégâts (pas sous 0). À 0 : ses processus s'arrêtent ; morts spéciales des humains (formes qui ne sont pas des robots, avec l'option `VIOLENCE=1` de l'exécutable, `DS:0953`, pour les explosifs et le type 14) : explosifs (3, 4, 10, 12), si les dégâts atteignent 3/5 (type 3) ou 2/5 d'un tirage de dégâts du type et que la forme 0x576 tient à sa place, le corps devient 0x576 (déchiqueté) : pose de l'action 1, 0 à 8 actions 1 en tournant au hasard, 0x12, 1 à 3 fois 0x14, puis 0x17, et il est mort ; feu (6) → forme 0x58F (brûlé), pose 0 ; type 14 → 0x596, pose 0x12 ; type 15 → 0x59C, pose 0x12 ; type 7 sur le joueur → action 0x37 ; puis la mort ordinaire ; sinon l'action de mort : 0x14 (chute en avant) quand le projectile allait dans la direction du PNJ à 3 près (ou +4), 0x12 (en arrière) quand il venait de face, au hasard à −4, 0x12 si la forme n'a pas d'action 0x14 ; enfin le PNJ est mort (`Actor_I_setDead` : statut 0x8000, fiche +0x13 bit 0).

## Objets touchés, explosions, dégâts de zone

**Fiche de dégâts** (`STATIC/DAMAGE.FLX` entrée 0, 6 octets par forme) : octet 0 bit 0 fiche présente ; bits 1-2 explosion à la casse (taille + 1) ; bit 3 explosion avec dégâts de zone ; bit 4 le script (événement 6) décide ; bit 5 l'explosion détruit l'objet ; bit 6 remplacé par une autre forme ; bit 7 image cassée prise dans les octets 2 à 4 (sinon image + octet). Octet 1 : son. Octets 2 à 4 : images (bit 6 : forme en 2-3, image en 4). Octet 5 : points de vie donnés à la création.

**Un objet touché** (`ITEM_10a8_275F`, mots 0x4000 et (type << 8) | dégâts) :
- fiche sans bit 4 : les dégâts s'appliquent (`ITEMDAM_10C0_030C`) ; si l'objet casse, ses processus s'arrêtent ;
- sinon, si l'objet a une fiche et ne casserait pas encore (`025D`), il perd seulement des points de vie ; autrement, si sa classe a l'événement 6, celui-ci est lancé avec les deux mots puis, sauf si l'objet est rangé (z 254), les dégâts s'appliquent ; sans fiche ni événement, rien (`CRUSADER_1130_1090` est vide).

**Appliquer les dégâts** (`ITEMDAM_10C0_030C`) : rien sans fiche, ni pour un objet déjà cassé par des dégâts (statut 0x100). Avec des points de vie : 0 dégât ne fait rien ; moins que les points de vie en retire ; sinon l'objet casse (points à 0). Casse : statut 0x100, `Item_I_setBroken`, explosion des bits 1-2 ; puis selon les bits 6 / 7 / aucun : forme de remplacement à la même place si elle y tient (`Item_I_legalCreateAtPoint`, qualité conservée), image tirée parmi les octets 2 à 4, ou image + un des octets 2 à 4 ; sans aucun de ces octets, l'objet cassé par des dégâts est détruit. La difficulté multiplie aussi les dégâts reçus par les objets (×5 au niveau 1).

**Explosion** (`SPRITE_1150_0891`, taille 0 à 2) : une seule fois par objet (statut 0x40). Rayon et dégâts sont ceux du type de tir 4 (10 et 150) divisés par 3, 2, 1 pour les tailles 0, 1, 2. Avec dégâts de zone, effet de zone du type 4 autour de l'objet (lui exclu) ; avec destruction, l'objet disparaît. Le sprite d'explosion est créé, puis le son 0x1C ou 0x6C (voir `audio.md`). `Item::I_explode` (`1150:0859`, intrinsic 0x017 d'après `USECODE/UNKCOFF.DAT`) l'appelle avec dégâts de zone.

**Dégâts de zone** (`TRACKER_1128_039C`) : les objets dont x et y sont à moins de rayon × 16 (recherche par rectangle dans l'ordre de la grille, `ITEMFIND_10C8_18C0`) reçoivent les dégâts (250 au plus ; divisés par distance / 48 pour les types 3, 4 et 10), avec la direction depuis le centre. Sont épargnés : les objets rangés, les projectiles, l'objet exclu et le second exclu, sauf si c'est le PNJ contrôlé. À l'impact d'un projectile dont le type de tir a un rayon (+6), l'effet de zone épargne l'objet touché et le tireur, sauf si c'est le joueur (`SPRITE_1140_1E09`) : on peut se blesser avec ses propres explosifs.

## Sprites (`SPRITE.C`, type 0x249)

Un sprite (`SpriteProcess`, créé par `1140:00DE`, intrinsic 0x0C7 `SpriteProcess::I_createSprite(forme, première, dernière, délai, x, y, z)`) montre les images `first` à `last` d'une forme à un endroit fixe. À chaque tour l'image avance d'un pas (recule si `last` < `first`) ; au-delà de `last` (ou sous 0) le sprite disparaît ; avec un délai, l'image est affichée et le processus attend ce nombre de ticks. Dix formes d'explosion et d'éclair (0x31C, 0x31F à 0x327, liste `DS:29C2`) sont reportées à z 240 au même endroit de l'écran, puis décalées (`DS:299A` / `DS:29AE`), pour être dessinées par-dessus la scène (`SPRITE_1140_0007`).

**Sprite d'impact** d'un projectile (`SPRITE_1150_02DF`, délai 3) selon le type de tir : 1, 11, 13 et 2 → étincelles 0x1D8 (une des plages d'images 0–4, 5–8, 9–12, 13–16) ; 3 et 4 → 0x326 / 0x320 / 0x321 ; 10 → 0x31C / 0x31F / 0x322 ; 6 → 0x578 ; 14 → 0x56B ; 15 → 0x59B ; autres → 0x537 (images 0 à 10) ; les explosions vont de l'image 0 à 0x27. **Explosion d'un objet** : 0x31C / 0x31F (taille 0), 0x326 / 0x320 / 0x321 (taille 1), 0x324 / 0x323 / 0x325 (taille 2), au centre de l'objet, images 0 à 0x27, un tick chacune.

Dans le portage, un sprite est un objet du monde marqué « sprite seulement » : sans collision, ignoré des recherches des scripts, de la zone active et de la liste des cibles ; l'original les garde hors de la table des objets (`SPMGR.C`), si bien que les numéros des objets créés ensuite peuvent différer. Non reproduits : le mode détail réduit (`DS:2887`, images de 2 en 2), la fusion des éclairs voisins (`DS:2888`), la translucidité (`TYPEFLAG` octet 1 bit 3), le rattrapage quand le jeu prend du retard. Les sons des impacts : voir `audio.md`.

## Arme superposée (`WPNOVLAY.DAT`, `WPNCACHE.C` segment `1160`)

Le joueur (objet 1, forme 1) est dessiné sans arme ; l'affichage (`DLIST.C`, `11A0:05xx`) dessine juste après lui, quand il est en combat ou que sa dernière action est 0xB (rengainer), une pièce d'arme : image de la forme de la classe de son arme, sans miroir, à sa position écran plus un décalage.

`STATIC/WPNOVLAY.DAT` : Flex de 64 entrées, une par action de la forme 1 ; seules comptent les actions qui ont des images et le bit 1 de l'octet 1 de leur en-tête. Une entrée contient 15 blocs, un par classe d'arme ; un bloc a, pour chaque direction puis chaque image de l'action, 4 octets : décalage x (signé), décalage y (signé), image de la forme de la classe (mot). En 8 directions, la direction est divisée par deux. Le chargement (`1160:03FF`) ne garde que le bloc de la classe de l'arme courante.

Classes (`DS:2B82`, par forme d'arme) et formes (`DS:2B64`) : 814, 815 → 0 (0x1A1) ; 816 → 1 (0x36D) ; 908, 819, 820, 906 → 2 (0x36F) ; 910, 904, 909, 907, 902 → 3 (0x36E) ; 818 → 4 (0x33B).

Vérifié : le joueur de `SAVEGAME.13` (fusil à pompe, action 0x19, image 9, direction 0) porte l'image 949 de la forme 0x33B, décalée de (10, 1).

## L'IA des ennemis

### Activités (`NPC.C`)

Chaque PNJ a une activité (fiche +2, la précédente en +4, trois activités par défaut en +6, +8, +0xA). `Actor_I_setActivity` (`10F0:2D02`, intrinsics 0x057, 0x07E, 0x0CE, 0x0DB, 0x0F2) :
- rien pour un PNJ mort, verrouillé (+0x5A bit 0) ou contrôlé ; 0x70 à 0x72 désignent les activités par défaut 0 à 2 ; en difficulté 4, une fois sur deux 5 devient 10 et 9 devient 11 ;
- rien si l'activité est 0 ou déjà en cours, ni si le PNJ est en combat (+0x59 bit 3), sauf pour l'activité 12 ;
- l'ancienne activité passe en +4 ; s'il était en combat, `Actor_I_clrInCombat` (rengaine l'arme par l'action 0xB si elle était sortie, arrête le processus d'attaque) ;
- dans la zone active : les processus du PNJ de types 0x204, 0xF0 (animations), 0x258, 0x257, 0x255, 0x259, 0x25F, 0x25E s'arrêtent, puis le processus de la nouvelle activité démarre : 1, 2, 3, 7, 8 et les autres → `TYPE_1108_*` (ci-dessous) ; 5, 9, 10, 11, 12 → combat.

**Combat** : le PNJ est « en combat » (`NPC_10f0_3183`) et un processus d'attaque est créé (voir ci-dessous). Venant de l'activité 8, il le note (+0x97). Activité 12 : combat limité à 8 à 17 secondes, puis activité 5 (`ATTACK_1110_1485`). Activités 9 et 11 : arme sortie (+0x5A bit 1), PNJ immobile (+0x7B), action 7 (dégainer) ; sinon arme rangée et action 0 ; le processus attend cette animation. Activités 10 et 11 : PNJ « prudent » (+0x7C).

`Actor_I_setTarget` (`1E71`, intrinsic 0x0E5) : un PNJ qui n'est pas en combat perd son processus d'attaque et prend l'activité 5 ; la cible est notée en +0x64. L'arme sortie (+0x5A bit 1) est posée par les actions 7 et 0xC et effacée par 0xB et 0x10 (`Actor_I_doAnim`).

**Entrée dans la zone active** (`Item_I_enterFastArea`, `ITEM.C 2CA8`) : l'activité et la précédente sont remises à 0, le PNJ prend la première image de l'action 0 (`CRUSADER_1138_32a8`), arme rangée, hors combat, puis son activité est rétablie. C'est ce qui démarre les ennemis. Dans le portage, le PNJ contrôlé est laissé de côté : la zone active est reconstruite au chargement d'une partie, là où l'original restaure l'état sauvegardé.

### Activités hors combat (`TYPE.C`, segment `1108`)

Chaque processus d'activité commence par montrer le PNJ debout (pose de l'action 0) arme rangée (`1108:0000`).

| Activité | Processus | Comportement |
|---|---|---|
| 1 | « Stand » (0x258) | toutes les 60 ticks, s'il repère le joueur (ci-dessous), prend son activité par défaut 1 |
| 2 | « Loiter » (0x257) | quand il n'est pas animé : une fois sur deux une gesticulation (action 0x1E ou 0x1F, puis 0) si sa forme l'a, sinon il marche (marcheur, 12 pas, arrêt à 0x40) vers un point à ±0xA0 de sa position de départ |
| 3 | (0x255) | s'il ne repère pas le joueur et n'est pas animé : un pas de marche simulé dans sa direction ; s'il passe, il marche (action 1) ; bloqué une fois, il s'arrête (action 0) et fait demi-tour, l'animation attendant 60 ticks ; bloqué deux fois, il se rend (activité 7), ou attaque (5) pour les formes 0x2F5 à 0x2F7, 0x344, 0x597 |
| 7 | « Surrendering » (0x25F) | mains levées (action 0x39), activité verrouillée (+0x5A bit 0, rendu à la fin) ; toutes les 60 ticks, il se tourne vers le joueur (`Actor_I_turnToward`) ou, déjà tourné et le voyant, supplie une fois (son par forme, voir `audio.md`) |
| 8 | « Guard » (0x25E) | animé : attend 60 ticks ; s'il voit le joueur : activité 5 ; sinon, une fois sur deux, attend 60, 120 ou 180 ticks, ou gesticule |
| 4, 6 | — | aucun processus |
| autres | « Stand » | |

**Repérer le joueur** (`1108:026E`) : PNJ en partie à l'écran, avec une activité par défaut 1 non nulle qu'il n'a pas déjà, et qui **voit** le joueur → `Actor_I_setActivity(0x71)`.

**Voir le joueur** (`1108:00C2`) : PNJ bien à l'écran (`ITEM_10a8_41f6`), tourné à deux directions près vers lui, et soit une ligne de tir (`Item_I_fireDistance` dans sa direction), soit, s'il n'a pas d'image de tir (`DS:2746`), `Item_I_getRangeIfVisible` (`1130:1C01`) : balayage de sa forme jusqu'au joueur (relevé de (hauteur − 1) × 8) dont le premier obstacle est le joueur ; distance / 16 arrondie, si elle est sous 49.

### Création de PNJ (`Actor_I_createActorCru`, œufs de monstres)

**Allocation** (`NPC_10f0_236b`) : un emplacement de PNJ libre (liste `ITEMCACH_10b0_268d` ; le portage prend le plus petit numéro libre de 2 à 255) ; sinon un PNJ jetable (statut 2) hors de la zone active, hors des formes 0x3AC, 0x4DC, 0x524, 0x4D9, 0x4E0, est détruit et sa place reprise. La fiche est remise à zéro, les points de vie viennent du type (`DTABLE.FLX`, +2), l'image vaut (direction + 4) & 15 ; formes 0x597 et 0x3AC : fiche +0x5A bit 2.

**`Actor::I_createActorCru(générateur, modèle)`** (intrinsic 0x129) : le numéro de PNJ du modèle donne le type (+0x18 arme, +0x1A autre arme, +0x3E forme), sa qualité haute l'activité par défaut 0, son numéro de carte l'activité par défaut 2 ; le générateur donne la qualité basse (fiche +0x63), la qualité haute (activité par défaut 1), le numéro de carte (bits 0-1 : difficulté minimale − 1, bit 2 : autre arme) et le numéro de PNJ (quartet bas : direction / 2, quartet haut : fiche +0x12). Rien au-dessus de la difficulté du jeu ; en difficulté 4, l'arme est tirée selon la forme (`ITEM_10a8_3b10`). Le PNJ est posé au générateur, qui devient sa position d'origine (+0x5E), statut 2, tactique 0 ; son arme est créée, rangée dans l'équipement (`Actor_I_setEquip` : objet mis dans le PNJ, statut 0x200) et rendue active.

**Œuf de monstre** (famille 7, forme 0x24F, `ITEMFIND_10e0_0247`) : en entrant dans la zone active, un œuf d'image 0 passe à l'image 1 et crée son PNJ comme ci-dessus, le modèle étant son premier contenu (qualité haute, basse et numéro de carte = activités par défaut 0, 1, 2), fiche +0x63 = 0, puis le PNJ prend son activité par défaut 0 ; l'événement 0x0F n'est pas lancé. 199 œufs dans `FIXED.DAT` (cartes 2, 6, 28, 47, 62, 69, 223, 245…), aucun dans les sauvegardes.

**Tactiques de `COMBAT.DAT`** : les deux créations ci-dessus posent la tactique 0, et les scripts n'appellent `Actor::I_setCombatTactic` (0x0DE) qu'avec 0 (classe `NPC`). L'interpréteur de tactiques (`ATTACK_1110_06B9`) n'est donc jamais utilisé dans cette version et n'est pas réécrit.

### L'alerte

**Drapeau d'alerte** (`World::I_setAlertActive` 0x0B5, `I_clrAlertActive` 0x0B7, `ITEMFIND.C` `10E0:0000` / `0121`) : un octet (`DS:135C`, remis à 0 au chargement et en nouvelle partie) et une recherche sur toute la carte (script `@ %n = … + $`) des formes 0x21, 0x49, 0x174, 0x271 (lumières d'alarme : image 0 éteinte, 1 allumée) et 0x477 (boîtier d'alarme : image 0/1, + 2 pendant l'alerte). Tout le reste est dans les scripts : `ALARMBOX equip(1)` ou `ALRMTRIG` lancent `FREE` événement 0x3B, qui lève l'alerte, joue la sirène propre à la carte (`FREE` 0x21, sons) et active les `ALARMHAT` (0x561) de la zone active à 1 600 unités du joueur ; `ALARMBOX use()` et `TELEPAD` l'éteignent. Quand la création d'un PNJ échoue faute de place pendant l'alerte, `createActorCru` éteint puis rallume les lumières ; un œuf de monstre fait l'inverse et laisse donc l'alerte éteinte (reproduit tel quel).

**Tir du joueur** (`CRUSADER_1138_2daf`, au plus toutes les 240 ticks : `CRUSADER_1138_2d72`) : chaque PNJ 2..255 sur la carte et à l'écran, autre que le PNJ contrôlé, se réveille. En combat, ou déjà dans son activité par défaut 2, ou d'activité verrouillée (+0x5A bit 0) : s'il monte la garde (8), il attaque (5) ; il vise alors le PNJ contrôlé (en combat : toujours). Sinon il prend son activité par défaut 2 (0x72) — s'il voit le PNJ contrôlé (`Item_I_getRangeIfVisible`) pour les formes 0x2F5, 0x2F6, 0x2F7, 0x595, 0x597, sans condition pour les autres — et vise le PNJ contrôlé si cela le met en combat. Le portage réveille toujours au premier tir après le chargement (l'original compare à 0 sur l'horloge du jeu).

### PNJ contrôlé (`World::I_setControlledNPCNum`, `CRUSADER_1138_2c02`)

Les commandes du joueur (marche et tir) vont au PNJ contrôlé (`DS:2852`) : un robot ou une caméra pris en main par les scripts (`CRUMORPH`, `SURCAMNS`, `SURCAMEW`, `NPC`), 0 pour aucun. En changeant : l'ancien, vivant, quitte le combat, et le mode de visée est coupé (`CRUSADER_1138_0692`) ; la caméra suit le nouveau (`CameraProcess_I_setCenterOn`) et y saute (`I_moveTo`) ; un nouveau qui n'est pas le joueur voit ses processus arrêtés et quitte le combat ; le curseur reprend l'image 0. Le drapeau `DS:2864` (« la forme n'a pas l'action 1 ») ne sert qu'au mode de visée.

### Mort du PNJ contrôlé

À 0 point de vie (`CRUSADER_1130_026b`), en plus du chemin commun (sons de l'objet coupés, processus du PNJ arrêtés, chute et mort), si la victime est le PNJ contrôlé : la marque de la cible désignée est effacée (`CRUSADER_1138_0459(0)`), puis, pour le joueur, un processus `DeathSilence` (type 0x261, minuterie à chaque tick) joue le son 9 au troisième tick et se termine ; pour un robot contrôlé, la main revient (`World::I_setControlledNPCNum(0)`). Il n'y a pas d'écran de fin : le jeu continue, le Controller refuse les commandes d'un PNJ mort (`CRUSADER_1138_12cf`), Échap ouvre le menu comme d'habitude (chargement). Le joueur n'a pas de classe usecode : `NPCDEATH` ne sert qu'aux autres PNJ (événement de mort des classes `GUARD`, `VARGAS`…). Seule la touche de triche F10 ressuscite le joueur (forme remise, `NPC_10f0_2037`). ScummVM ajoute un `AvatarDeathProcess` repris d'Ultima 8 qui ouvre le menu après 5 secondes ; ce n'est pas dans l'exécutable.

### Le processus d'attaque (`ATTACK.C`, type 0x259)

Construction (`1110:0000`) : délais selon l'arme (formes 0x386, 0x388, 0x38E : 60 ticks avant le premier tir, puis 120 / 90 entre deux salves en difficulté 1 / 2, sinon 15 pour la forme 0x3AC et 60 ; autres armes : 30, puis 30 / 20 / 15 / 0 en difficulté 1 / 2 / 3 / autre ; sans arme : 0), tactique (`COMBAT.DAT`, fiche +0x5C ; 0 = comportement intégré), cible remise sur le joueur, +0x68 = 0, PNJ dans la pose de l'action 0 et arme rangée.

Chaque tour de la tactique 0 (`1110:14F6`), si le PNJ n'est pas déjà animé :
1. La cible devient le PNJ contrôlé (1 s'il n'y en a pas). Cible absente, rangée (z 254), morte ou hors de l'écran : cible 0, et un PNJ mobile part vers un point au hasard à ±0x200.
2. Combat limité (activité 12) : à l'échéance, activité 5. Sinon, une fois sur trois : une fois sur trois il se tourne au hasard, sinon il tire s'il fait face à la cible ; les autres fois il sort ou range son arme.
3. Direction de la cible (`getClosestDirectionInRange`, en 8 ou 16 directions selon son action de garde, 0 ou 4). Si la distance (Tchebychev) est au plus le double de l'écart de hauteur, ou si un PNJ mobile n'est pas assez à l'écran (moitié de la largeur et toute la hauteur de son image, `ITEM_10a8_41f6`), il s'approche (voir « Se déplacer »).
4. **Face à la cible** : tirage de 0 à 9 ; hors de l'écran, attente de 20 ticks. Il ne continue que s'il vient de se déplacer, s'il a commencé à tirer il y a moins de 120 ticks, si le joueur n'a pas bougé récemment, sur un tirage < 3, ou < 4 s'il est prudent ; sinon attente de 20 ticks. Il peut toucher s'il a été touché il y a moins de 120 ticks, s'il tire depuis peu, s'il a vu la cible récemment (60 ticks), ou si `Item_I_fireDistance` trouve une ligne de tir ; sinon il s'approche (mobile) ou attend 15 ticks (immobile). Au premier passage, une fois sur trois, il pousse un cri selon sa forme (tables `DS:1FFD` à `DS:2031`) ; ensuite, toutes les 480 ticks pour les formes 0x2DF et 0x383. Arme rangée : il la sort (action 7). Puis il attend le délai avant le premier tir, et celui entre deux salves. Enfin il tire (action 8) une salve du nombre de coups de l'arme ; à partir de 3 coups, une fois sur cinq (une sur deux en mode accéléré), une longue salve de quatre fois plus (+0x68 = 1). À la fin de chaque animation de tir, le coup suivant part vers la cible (`1110:22CA`) ; après le dernier, si le joueur bouge, une fois sur deux il attend 60 ticks (sauf formes 0x338 et 0x4E6).
5. **Pas face à la cible** : s'il tire depuis peu, ou si une ligne de tir existe en 16 directions, il l'a vue. Arme rangée et cible vue : il sort son arme. Cible vue ou PNJ immobile : il se tourne vers elle. Sinon il s'approche.

### Se déplacer (« PathFinder », `ATTACK.C` segment `1118`, type 0x204)

Ce n'est pas une recherche dans un graphe : à chaque tour, le processus choisit la direction du prochain pas en **simulant** une image de la marche (action 1, ou 0x24 en combat), puis fait jouer la vraie marche dans cette direction et l'attend. Créé par `1118:0000` (vers un point) ou `1118:01DC` (vers un objet, dont la position est relue à chaque tour s'il s'agit d'une forme solide ayant une hauteur), avec un nombre de pas maximal, une distance d'arrêt et une rotation finale. Un seul par PNJ (fiche +0x59 bit 5 : l'ancien s'arrête). Au départ, un PNJ en combat l'arme sortie la rengaine (action 0xB, `1118:03CC`) ; une table de candidats est tirée au hasard.

**Simulation** (mode test d'`ANIMPRIM` : le constructeur reçoit un pointeur de position) : la première image de l'action est jouée depuis la position fictive, avec le même balayage que le vrai pas ; la position avance si le pas passe. Un pas bloqué fait échouer l'animation, ce que le marcheur apprend par la notification de fin (drapeau 0x8000 de l'écouteur, `NOTLIST.C`). Un pas qui touche l'objet visé n'échoue pas et le signale (résultat bit 1).

**Choix d'une direction** (`1118:08E4`), depuis la position p :
1. d = direction (8 directions) de p vers le but. Si p est plus proche du but que jamais (Tchebychev en x et y), un pas vers d est simulé ; s'il passe, le PNJ cesse de longer un mur.
2. Candidats : d + {0, +2, −2, +4, −4, +6, −6, −8} (`DS:208B`), ou la table miroir `DS:2093`. En longeant un mur : depuis l'inverse de la dernière direction, en tournant de 2 du côté choisi, les candidats de rang 2, 4, 5, 6 et 7 de cette liste.
3. Le premier candidat dont le pas simulé passe est retenu. Toucher l'objet visé (forme solide avec hauteur), ou arriver à la distance d'arrêt, termine la marche après ce pas. Aucun candidat : pas de chemin.
4. Un candidat autre que d fait longer un mur, du côté donné par la parité de son rang.
5. En combat, si le PNJ a désormais une ligne de tir vers sa cible, il s'arrête (action 0), prévient son processus d'attaque qu'il l'a vue (`ATTACK_1110_252d`), et la marche se termine.

**Chaque tour** (`1118:0428`) : sur les x et y du but, fin (réussie si z coïncide) ; pas de chemin ou nombre de pas atteint : échec ; sinon rotation vers la direction (`SPRITE_1150_009C`) et une animation de marche. **À la fin** (`1118:072E`, réussite comme échec) : bit 5 effacé ; avec rotation finale, le PNJ se tourne vers le but ; s'il ne s'est pas tourné, il reprend sa garde (action 4 en combat arme sortie, sinon 0).

Le processus d'attaque s'en sert pour s'approcher de sa cible (12 pas au plus, arrêt à 0x40, `ATTACK_1110_141B`) et pour errer quand il l'a perdue (point au hasard à ±0x200, 12 pas, arrêt à 0x40, rotation finale).

**Rotation** (`SPRITE_1150_009C`) : du côté le plus court, une animation de garde par direction (par pas de 2 en 8 directions). L'action est 0x2E à genoux, 0 (0x3C si verrouillé) hors combat, 4 en combat (0x25 pour le joueur dont l'arme remplace les actions). Un écart d'une seule direction est ignoré, sauf demande expresse.

**Ligne de tir** (`Item_I_fireDistance`, `1130:1755`, intrinsic 0x116) : pour un PNJ, les points de départ sont les bouches du canon des deux premières images de tir de son action de tir (8 ; à genoux 0x2A ou 0x2B ; pour le joueur 8 ou 0xD) dans la direction donnée ; sinon, le décalage passé en argument. Depuis chaque point : s'il y a déjà un objet, réussite seulement si c'est la cible ; sinon, balayage avec la forme 0x1D9 vers le point visé de la cible (`CRUSADER_1130_00ee`), réussi s'il ne rencontre rien ou la cible. Le résultat est la distance en x et y divisée par 16 : à moins de 16 unités, il vaut 0, ce qui compte comme un échec.

## État

Vérifié (`game_tests`) sur `SAVEGAME.13` : contrôlé, le garde 2 avance de 197 unités sous la flèche haut sans que le joueur bouge, et sa mort ne laisse aucun PNJ contrôlé ; le PNJ 6 changé en Vargas ne perd aucun point de vie, son bouclier passe de 5 000 à 4 970 (action 0x21), un second coup pendant l'éclat ne fait rien, et bouclier vide il perd 30 points. Sur `SAVEGAME.1` : `ALARMBOX equip(1)` lève l'alerte, le boîtier 298 passe de l'image 0 à 2 et les 41 lumières d'alarme de la carte s'allument ; `use()` éteint tout. Sur `SAVEGAME.13` : un tir du joueur réveille le PNJ 6 posé à l'écran (activité 0 → 5, en combat, cible le joueur) ; `createActorCru` crée un PNJ de forme 765 (35 PV, activités 8 / 1 / 5, arme active) ; un œuf de monstre amené dans la zone active crée un garde armé une seule fois ; le PNJ 8 (activité 2) flâne autour de sa place (jusqu'à 182 unités), le PNJ 5 (activité 3) marche droit devant (1 212 unités en 10 s), le PNJ 12 mis en garde (8) et tourné vers le joueur passe à l'attaque (5) ; le PNJ 2 tué par le feu devient 0x58F, par un explosif 0x576 (séquence jusqu'à 0x17), par le type 15 0x59C ; un pistolet posé près du joueur, touché par un explosif, reçoit une vitesse vers l'est (lancé à plat, il se pose aussitôt dans le portage ; dans l'original aussi, un objet sans drapeau de rebond s'arrête en touchant le sol, mais le traitement d'un contact présent dès le départ par le balayage reste à vérifier) ; un missile guidé tiré par le PNJ 2 deux directions à côté du joueur vire et le touche ; un tir de type 5 sur un objet 0x2E posé devant le joueur repart vers l'ouest ; le fût 2370 (forme 0x1FD, 50 PV) perd 20, puis casse, explose et laisse place à la forme 0x568 image 1 ; l'objet 2427 (0x2A5) explose (zone) et passe à l'image suivante, ses deux sprites d'explosion apparaissent puis disparaissent avec leurs objets ; les plombs du fusil à pompe font jaillir des étincelles (capture `--fire`) ; le garde (PNJ 2, activité 9) placé devant le joueur obtient son processus d'attaque, se tourne vers lui, tire après 82 images, 17 coups en 10 secondes, et le joueur passe de 141 à 91 points de vie ; le PNJ 6 (activité 0, activité par défaut 2 = 5), touché, passe en combat et vise le joueur ; le PNJ 16 (activité 5) posé à 750 unités à l'est, hors de l'écran, marche vers le joueur (de 750 à 610 unités), s'arrête quand il peut tirer et tire ; avec le bouclier 1, un tir de pistolet coûte 80 d'énergie au lieu de points de vie, et blesse quand l'énergie manque. Également : la liste des cibles suivies (23 objets, tous dans la zone active, non cassés, de forme visable) ; un PNJ placé à 256 unités, 8,9° à côté de la direction du tir, est trouvé et visé à z + 64 : un tir droit devant passe à côté, un tir visé le tue. Également : remplacement des actions pour le fusil à pompe (8→0x36, 0x2A→0x32, 7→0xC) ; une pression brève arme la pompe (action 0xF), la suivante tire (action 0x36, une cartouche, 8 plombs qui vont frapper le décor à environ 450 unités), la suivante recharge (12 cartouches, 15 boîtes au lieu de 16). Un tir de pistolet sur un PNJ placé devant le joueur (forme 765, 35 points de vie) le tue : 0 point de vie, mort, action 0x14.

Les sons (tirs, impacts, explosions, cris, mort du joueur, sirène des scripts) sont décrits dans `audio.md`.

Non reproduits : autres touches du Controller, le détail de la minuterie pendant le sommeil du Controller (le portage n'envoie pas de message 2 pendant l'attente) ; réparation d'un objet (`ITEM_10a8_260f`, pas encore appelée) ; pour les tirs : la traînée, le point d'impact au pas de 16 près (le portage prend l'arrêt du balayage), le curseur au changement de PNJ contrôlé. Pour l'IA : la gestion des messages du marcheur par « Loiter » (`1108:0D3E`), l'abandon d'une salve quand l'animation a échoué, le mode « plan » du marcheur (chemin entier simulé d'un coup, inutilisé par l'attaque), la chute pendant une simulation (un pas sans appui compte comme bloqué), et le moment où part le coup suivant d'une salve (au réveil du processus plutôt qu'au message de fin d'animation). Le projectile est dessiné comme un objet du monde de forme 0x1D9 marqué « sprite seulement » (ignoré par les collisions) au lieu d'un sprite.
