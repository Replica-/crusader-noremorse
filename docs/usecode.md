# Interpréteur usecode

Les scripts du jeu (`USECODE/EUSECODE.FLX`, 402 classes) sont du bytecode exécuté par l'interpréteur de `INTER.C`. La réimplémentation se trouve dans `src/engine/usecode/`. Le désassemblage lisible est produit par `tools/usecode_disasm.py`.

## Format des classes

| Position | Contenu |
|---|---|
| entrée Flex 1 | noms de classes, 13 octets chacun à partir de l'offset 4 |
| entrée Flex `cid + 2` | la classe `cid` |
| `+8` | `u32` = base du code + 1 |
| `+20` | table des fonctions : 6 octets par entrée (`u16` ?, `u32` offset relatif à la base) |
| `base + 1…` | bytecode ; les pointeurs d'instruction sont relatifs à `base` |

**La table se termine exactement à `base + 1`** : l'octet en `base`, que l'on prenait pour un octet de remplissage, est le dernier octet de la dernière entrée. Le nombre de fonctions est donc `(base + 1 − 20) / 6`. C'est vérifié sur les 402 classes.
- La formule `(base + 19) / 6` utilisée par ScummVM compte environ 6 entrées de trop, lues dans le code.
- La formule `(base − 20) / 6` en oublie une : par exemple la fonction 35 de `TEXTFILE`, appelée par `MONITNS`.

## Particularités de Crusader

- `call` (0x11) et `spawn` (0x57) prennent un **numéro de fonction**, résolu via la table de la classe cible, et non un offset.
- Les variables globales (0x4E/0x4F/0x79) sont dimensionnées en **octets**, pas en bits comme dans Ultima 8. Elles tiennent dans 0x42 octets : `UNKDS.DAT` donne leurs valeurs initiales (`Flags_load`, nouvelle partie), l'entrée 60 des sauvegardes leurs valeurs en cours (voir `docs/missions.md`).
- `spawn_inline` (0x58) n'est jamais utilisé. Seules les recherches d'objets de type 2 (zone), 4 (conteneur) et 6 (surface) apparaissent.
- La table des intrinsics (fonctions moteur appelées par `calli`) est dans `UNKCOFF.DAT` : 309 entrées (offset, segment NE). Elle correspond à la table française de ScummVM.
- **Classe qui reçoit les événements d'un objet** (`Usecode_class_of`, `1420:0DC5`, `World::usecode_class`). `Usecode_init` (`1420:0CDF`) découpe les classes en quatre plages, de tailles prises à `DS:65F0` :

  | Plage | Pour | Classe |
  |---|---|---|
  | 0x000–0x7FF | les formes | numéro de forme |
  | 0x800–0x8FF | un PNJ qui n'a pas été créé par un œuf (bit 0x0002 du statut absent) | 0x800 + son numéro ; seule 0x801 `AVATAR` existe |
  | 0x900–0x9FF | les œufs de famille 4 | 0x900 + type d'œuf (`Item::I_getUnkEggType`, octet bas de la qualité) |
  | 0xA00–0xBFF | les classes libres | appelées seulement par les scripts |

  Les classes d'œufs sont celles des déclencheurs et des scripts de mission : `TRIGEGG`, `ONCEEGG`, `CAMERA`, `DOOREGG`, `MUSIC`, `MISS1EGG`… `MISS15`. Les autres PNJ placés sur les cartes ne reçoivent donc aucun événement ; ceux des œufs à monstres reçoivent ceux de leur forme. Jusqu'au jalon 6, le portage envoyait tout à la classe de la forme : aucun script d'œuf ne tournait. `Egg::I_getEggId` (0x041) lit l'octet « carte » de l'objet, comme `getMapArray`.
- **`Item::I_equip`** (0x006, 0x0B6, `10A8:2A35`) n'équipe rien dans Crusader : il lance l'événement 10 de la classe de l'objet, si elle l'a (`Usecode_start_event_if_present`, masque 0x400), avec la valeur passée, étendue à 4 octets, comme argument. Il renvoie le pid. Les interrupteurs, déclencheurs, alarmes et scripts de mission s'en servent pour activer d'autres objets, par exemple les tapis roulants (`CONV_EL`) de `SAVEGAME.13`. `Item::I_unequip` (0x0B1, `10A8:2A68`) fait de même avec l'événement 11 (masque 0x800), et `Item::I_avatarStoleSomething` (0x0B2, `10A8:2FDA`) avec l'événement 0x13 (masque 0x80000), sauf pour un PNJ mort (rend 0).
- `Item::I_getFamilyOfType(forme)` (0x0AD, `1100:054D`) rend la famille de `TYPEFLAG.DAT` d'une forme, comme `I_getFamily` (0x03F) pour un objet. `Item::I_touch` (0x025, `10A8:2514`) marque l'objet à redessiner dans la liste d'affichage (`11A0:10E5` / `13BE`), sauf s'il est éthéré ; le portage redessine toute la vue à chaque image et n'en fait rien.

- **Tests d'emprise** (`ITEM.C`). `Item::I_getFootpadData` (0x066, 0x127) rend l'empreinte, x et y échangés pour un objet en miroir (statut 0x20). Un objet en (x, y, z) couvre x − 16 fx … x, y − 16 fy … y, z … z + 8 fz ; x et y sont comparés en non signé sur 16 bits, z en signé. Un objet rangé dans un conteneur (statut 8) ne touche rien.

  | Intrinsic | Vrai quand l'objet… |
  |---|---|
  | `I_overlaps` (0x0A5) | recoupe l'autre (intervalles ouverts sur les trois axes) |
  | `I_isOn` (0x046 et 12 alias, dont 0x126) | recoupe l'autre en x et y et repose sur son sommet (z = z' + 8 fz') |
  | `I_isCompletelyOn` (0x02B) | repose sur son sommet, l'empreinte entièrement dans la sienne |
  | `I_isCentreOn` (0x05A) | repose sur son sommet, son centre (`getCX` / `getCY`, sans miroir, sans test de conteneur) strictement dans l'empreinte de l'autre |

  Vérifié sur `SAVEGAME.1` : le joueur est « sur » l'objet trouvé sous lui.
- **Déplacements et créations sous condition**. `Item::I_legalMoveToPoint(objet, point, mode, drapeaux)` (0x031, `10A8:174D`) balaie l'objet de sa position (celle du conteneur le plus extérieur) jusqu'au point, avec les drapeaux de balayage (4, sans sol, quand ils valent 0). Arrivé : l'objet y est déplacé, retour 1 ; sinon, sauf si le bit 0 de `mode` est mis, il est déplacé jusqu'où il est allé, retour 0. `CurrentMap::I_canExistAt(forme, x, y, z)` (0x0BC, `1150:0B67`) teste la place libre (`docs/animation.md`) avec l'empreinte de la forme prêtée à la forme 0x31A : le masque et la hauteur minimale de 1 sont ceux de 0x31A (solide, non fixe). `Item::I_legalCreateAtPoint` (0x05D) et `I_legalCreateAtCoords` (0x019) créent l'objet sur la carte si sa place est libre. `Item::I_getEtherealTop` (0x023) rend le sommet de la pile des objets en attente (`DS:7DD4`), 0 si elle est vide. Non reproduits : les événements de contact du déplacement (`10E8:123A`) ; `CurrentMap::I_canExistAtPoint` (0x032), dont l'ordre des arguments reste à lire.

## Modèle d'exécution

- **Pile** de 4 Ko par processus, croissant vers le bas ; `bp` et `sp` sont des offsets dans cette pile.
- **Cadre d'appel** : arguments, puis pointeur `this` (empilé en dernier, donc en `bp+6`), puis classe, `ip` et `bp` de l'appelant (en `bp+4`, `bp+2`, `bp+0`). Les variables locales sont en `bp` négatif, réservées par `init`.
- **`this`** pointe sur une copie de l'objet (son identifiant, 2 octets) posée au fond de la pile du processus.
- **Retour** : `ret` depuis le cadre initial termine le processus, avec la valeur du registre résultat (posée par `pop_res`, 0x08). Les valeurs de retour de fonctions et d'intrinsics passent par un registre temporaire (`push_retval`, 0x5D à 0x5F).
- **`calli`** ne dépile pas ses arguments : l'appelant les retire avec `add_sp`.
- **`spawn`** crée un processus et l'exécute immédiatement jusqu'à sa première suspension. Son pid est placé dans le registre temporaire.
- **`implies`** (0x54) : le second pid dépilé attend le premier. `push_proc_result` (0x6D) relit ensuite le résultat de celui qui l'a réveillé.
- **Pointeurs** 32 bits opaques : `pid:offset` pour une pile, `0xFFFE:offset` pour les globales, `0xFFFD:handle` pour une chaîne, `0xFFFC:id` pour un objet.
- **Chaînes et listes** : handles 16 bits sur un tas. Les instructions qui les lisent depuis une variable les dupliquent ; les opérations combinées libèrent leurs opérandes.

## Recherches d'objets (`loop`, `ITEMFIND.C`)

L'opcode `loop` (0x70) dépile deux mots `a` et `b`, lit un type de recherche et un script de filtre (au plus 32 octets). L'original construit un objet de recherche sur la pile du script et l'avance à chaque `loopnext` ; le portage rassemble les objets au début de la boucle, dans le même ordre, et saute ceux détruits entre-temps.

| Type | Recherche | Arguments | Emplois dans les scripts |
|---|---|---|---|
| 2 | zone : objets dont x et y sont à `b` près de ceux de l'objet `a` | `a` = centre, `b` = portée | 413 |
| 3 | zone, variante (table virtuelle `65E8`) | | 0 |
| 4 | contenu d'un conteneur | `b` = conteneur | 28 |
| 5 | contenu récursif | | 0 |
| 6 | surface : objets posés sur `a` (base au sommet de `a`), sous `b` (sommet à la base de `b`), ou les deux autour de `b` si les deux sont donnés (`0xFFFF` = absent) ; les empreintes doivent se recouvrir | | 58 |

**Ordre** : les recherches de zone et de surface parcourent la grille de la carte (cases de 512 unités) en x croissant puis en y croissant, chaque case dans l'ordre de sa chaîne (dernier objet posé en tête). Le monde garde ces chaînes (`World::cell`), relues depuis l'entrée 1 des sauvegardes.

**Filtre** (`ITEMFIND_10c8_010c`) : petite machine à pile de mots de 16 bits, comparaisons signées. Un script vide de résultat est vrai ; sinon le sommet de pile décide.

| Octet | Effet |
|---|---|
| `0`, `1` | empile 0, 1 |
| `#` `@` `` ` `` `*` `?` | empile l'identifiant, la forme, l'image, la qualité, les drapeaux |
| `:` | empile la famille (14 pour une forme ≥ 0x800) |
| `%` | empile le mot qui suit |
| `<` `>` `=` `(` `)` | a < b, a > b, a = b, a ≤ b, a ≥ b |
| `&` `+` `!` | ET, OU, NON bit à bit |
| `A`…`_` | forme dans une liste de (octet − 0x40) mots |
| `a`…`0x7F` | image dans une liste de (octet − 0x60) mots |
| `$` | fin |

Les scripts du jeu n'emploient que `$ = % @ :` et les listes de formes. Les « chemins rapides » de l'original (`$` seul, `@ % n = $`, `: % n = $`, liste de formes) donnent les mêmes résultats, sauf qu'une recherche de surface ne s'y exclut pas elle-même.

## Objets du décor (lot 6.3)

Tout le comportement est dans les scripts ; le moteur n'y fournit que des intrinsics.

**Coffres** (`CHEST_NS` 0x54F, `CHEST_EW` 0x550 ; ouverts : 0x54A, 0x54B). `use()` les ouvre. `FREE` 0x2D change la forme si la nouvelle est de la même famille (`getFamilyOfType`) et que sa place est libre : il soulève le coffre, crée un objet d'essai (`legalCreateAtCoords`), le détruit, puis change la forme et l'image et repose le coffre. Réussi : son 0xB3, puis les images 0 à 4 (coffre à l'image 0) ou 5 à 9 (image 1), une tous les dix tics. Sinon un son d'échec (0x2A ou 0xB0 selon le coffre) et rien d'autre. Puis chaque **marqueur de contenu** (forme 0x476, invisible) posé à moins de 12 × 32 unités du coffre et de même qualité basse devient un objet (`FREE` 0x2E) : forme = octet « carte » + (octet « PNJ » & 0xE0) × 8, image = octet « PNJ » & 0xF, qualités recopiées ; l'objet est posé à la place du marqueur, reçoit `unequip(0)`, et le marqueur est détruit. Un objet de famille 10 à 13 (armes, munitions, objets utilisables) va au joueur (`addItemCru`, des munitions se combinent avec celles qu'il porte) ; s'il n'y tient pas, il tombe à ses pieds avec le son 0xB0. Les autres redeviennent un marqueur (`FREE` 0x31), après `avatarStoleSomething`. Un coffre ouvert (image > 1) ne fait plus rien.

**Cadavres** (`D_MAINT` 0x4BB, `D_GUARD` 0x339, `D_SOLD` 0x4BF, `D_CHEM` 0x4B9…) : `use()` donne leur contenu au joueur (`FREE` 0x40, recherche dans le conteneur) puis lève le statut 0x1000, qui empêche une seconde fouille.

**Moniteurs de surveillance** (`SURCAMNS` 0x4C6, `SURCAMEW` 0x4DE). `use()` lance l'événement 0x20 si le statut 0x1000 est libre. Celui-ci prend 0x1000, affiche les textes liés (`TEXTFILE`), puis lance l'événement 0x21 et rend 0x1000 à la fin. L'événement 0x21 met le joueur en stase (`setAvatarInStasis`, `setCruStasis`), pose un objet 0x471 à côté du moniteur, attend 30 tics, puis cherche un œuf `EVENT` (0x361) de même qualité basse à moins de 1000 × 32 unités. Ce qu'il fait dépend de la qualité basse du moniteur :

- **0xFE**, prise de contrôle. Écran noir, caméra sur l'œuf, palette grise. Il cherche le premier PNJ de forme 0x338, à moins de 40 × 32 unités de l'œuf, dont l'octet +0x63 vaut 0xFE (`Actor::I_getUnkByte` 0x00B ; c'est la qualité basse de l'œuf qui l'a créé). S'il le trouve, le joueur prend son contrôle (`setControlledNPCNum`), et le script attend que le PNJ contrôlé redevienne le joueur. **Échap** le rend (`1138:0896` : sans Ctrl+Alt, quand le PNJ contrôlé n'est pas le joueur, Échap le lui rend au lieu d'ouvrir le menu), y compris quand le robot est mort. Sans PNJ, la vue reste jusqu'à ce qu'une touche soit tenue.
- **0xFD** : la vidéo `3j` après 60 tics.
- **0xFA à 0xFC et 0xFF** : caméra sur l'œuf, puis l'événement 0x22 donne le contrôle d'un PNJ de forme 0x1CD à 0x4C8 proche de l'œuf ; s'il n'y en a pas, la vue dure 200 tics.
- **Jusqu'à 0xF9**, la vue simple. Écran noir, barre d'état cachée, caméra sur l'œuf, palette grise, puis le `TRIGGER` de l'œuf. Après 120 tics, la vue reste jusqu'à ce qu'une touche soit tenue.

Chaque cas finit par un écran noir, la caméra sur le joueur, la barre d'état et la palette normale. Puis le script détruit l'objet 0x471, sort le joueur de la stase et vide le clavier (0x0D3). « Une touche tenue » est `Ultima8Engine::I_moveKeyDownRecently` (0x096, `10A8:4340`). Malgré son nom ScummVM, elle rend `DS:322A`, le nombre de touches enfoncées que compte l'interruption du clavier (`11D8:3574` : +1 à l'appui, hors répétitions ; −1 au relâchement), quelles que soient les touches.

**Caméras de sécurité** (`CAMERANS` 0x1B8, `CAMERAEW` 0x1D7, `CAMERACO` 0x1B6). À l'entrée dans la zone active (événement 0x0F), une caméra à l'image 9 est hors service et ne fait rien. Les autres attendent d'être à l'écran, puis balaient leurs images une à une (son 0xB5). Quand une caméra est tournée vers le PNJ contrôlé (`getClosestDirectionInRange`) et le voit (`Item::I_getRangeIfVisible` 0x044, la ligne de vue de l'IA, `1130:1C01`), elle s'arrête sur lui et compte. Au-delà d'un seuil qui dépend de la difficulté, et sans le statut 0x1000, elle émet le son 0xE6 et lance son `TRIGGER` (l'alarme), puis prend 0x1000. Avant 0x044, elles ne voyaient jamais le joueur.

`Item::I_move` (0x06C, `10A8:1531`) sert aux ascenseurs (`ELEVATOR`, `ELEVAT`), aux portes `DOOR2` et à certains interrupteurs, qui déplacent leurs marqueurs. Il retire l'objet de sa case ou de son conteneur et le pose aux coordonnées (`World::move_item`) ; la marque de la cible désignée le suit. Les activités par défaut s'écrivent par `Actor::I_setDefaultActivity0..2` (0x054 à 0x056, `10F0:2BEA`, `2C07`, `2C24`), qu'appelle le panneau 2571 de `SAVEGAME.7`.

Le reste du décor marchait déjà : interrupteurs, vannes, boîtiers d'alarme, panneaux, distributeurs. Les ordinateurs ont suivi (`ComputerGump::I_readComputer` 0x0FE, `UCMachine::I_numToStr` 0x0FF, 0x113 et 0x122 ; voir `docs/interface.md`, « Écran des ordinateurs »), puis les claviers à code (`KeypadGump::I_showKeypad` 0x0C5 ; voir « Clavier à code »), puis les moniteurs et les caméras. `useprobe` (ci-dessous) ne trouve plus d'intrinsic manquante dans les sauvegardes 1 à 7.

Vérifié (`game_tests`) :

- **Moniteur 3224** (`SAVEGAME.7`, qualité basse 0xFE). Le joueur prend le contrôle du robot 10. Les flèches le font avancer ; Échap rend la main sans ouvrir le menu, et la caméra, la barre d'état et le joueur sont rétablis.
- **Moniteur 3224, octet du robot mis à 0.** La vue de l'œuf reste en gris jusqu'à ce qu'une touche soit tenue.
- **Caméra 6082** (`SAVEGAME.2`). Joueur derrière un mur : elle balaie ses images 0 à 4, sans alarme. Joueur devant elle, à la portée 16 : bip, statut 0x1000 et alarme.

Vérifié (`game_tests` sur `SAVEGAME.1`) : le coffre 3169 s'ouvre (0x54A, image 4), son marqueur 3156 disparaît et le joueur reçoit un objet 0x33E ; le coffre 3160 ajoute ses deux marqueurs de 0x33D aux munitions du joueur (4 → 6), et un second usage ne donne rien ; le cadavre 8389 donne ses crédits (105 → 110) et prend le statut 0x1000. Aucune intrinsic manquante.

## Validation

- `tests/usecode_tests.cpp` : bytecode assemblé à la main. Couvre l'arithmétique, une boucle avec variables locales, un appel avec valeur de retour, une liste parcourue par `foreach` (et libérée), un appel d'intrinsic, `spawn` + `implies` avec récupération du résultat, et un opcode invalide.
- `ucsmoke` lance **chacune des 977 fonctions d'entrée** du jeu sur un objet fictif. Toutes les intrinsics y renvoient 0 et les recherches d'objets ne trouvent rien.
  - Sur 30 tranches : 1,3 million d'instructions exécutées, 166 intrinsics distinctes appelées, une seule erreur.
  - Sur 120 tranches : 5,1 millions d'instructions, toujours la même erreur unique : une division par zéro dans `HOVER`, dont le diviseur vient d'intrinsics qui renvoient 0.
  - Aucun opcode inconnu, aucun débordement de pile, aucun appel vers une fonction absente.

- `useprobe [données] [sauvegarde] [--shape S] [--frames N] [--verbose]` charge une sauvegarde et « utilise » chaque objet de la zone active dont la classe a un événement `use()` (un par forme et image), sur une copie neuve à chaque fois. Il affiche le changement de forme ou d'image, le nombre d'objets créés ou détruits et portés par le joueur, les erreurs et les intrinsics manquantes ; `--verbose` détaille les objets touchés et les marqueurs de contenu voisins.

Ces tests valident le décodage et la gestion de la pile. Ils ne valident pas le comportement de jeu, qui dépend des intrinsics, encore factices.
