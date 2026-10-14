# Parties sauvegardées (`SAVEGAME/SAVEGAME.N`)

Une sauvegarde est une archive **Flex** de 1 024 entrées. Elle est écrite par `CRUSYS_1058_0115` et relue par `CRUSYS_1058_02f1`. Chaque sous-système écrit ses propres entrées à travers un flux commun : méthodes virtuelles `+0x24` (choix de l'entrée), `+0x4` (début), `+0x10` (écriture d'un bloc) et `+0x8` (fin).

## Entrées

| Entrées | Sous-système (fonction de sauvegarde) | Contenu |
|---|---|---|
| 0 | `ITEMCACH_10b0_0b67` | **table des objets** (voir ci-dessous) |
| 1 | `ITEMCACH_10b0_0b67` | listes des objets : numéro de la carte en cours (2 octets, `DS:10FE`), zone active (4 × 2 octets signés), grille des têtes de chaîne (0x2000), grille des compteurs (0x2000), 8 × 2 octets (voir plus bas) |
| 9 | ? | 32 octets, commence par du texte (nom de la sauvegarde ?) |
| 10 | `NPC_10f0_00bc` | table des PNJ (`7E0A`) : 256 enregistrements de 0x79 = 121 octets, un par identifiant 0 à 255 (voir plus bas) |
| 20 | `NPC_10f0_390d` | bloc de 21 octets copié depuis `DS:7E0E` : +0 identifiant du joueur (1), +2 indicateur « occupé » (`DS:7E10`), +0xB difficulté (`DS:7E19`, `World_I_gameDifficulty` ; 1 dans toutes les sauvegardes du jeu), +0xD cartes magnétiques (`DS:7E1B`, un bit par carte), +0x11 batterie (`DS:7E1F`) ; relus par le portage sauf +2, le reste non décodé |
| 30–32 | `SPMGR_11a8_0eef` | gestionnaire de sprites (1, 2, puis taille variable) |
| 40–41 | `YAMM_1428_05ac` | 2 octets, puis 64 000 octets |
| 50–52 | `TRACKER_1128_05b7` | suivi |
| 60 | `Flags_save` (`FLAG_1410_02e7`) | globales usecode, 0x42 octets ; valeurs initiales dans `USECODE/UNKDS.DAT` (voir `docs/missions.md`) ; relue par le portage |
| 70–71 | `STDINT_1200_0311` | interruptions |
| 100+ | `KERNEL_11d8_2491` | noyau : en-tête `<Kernel Ver 2.00 …>`, table des processus, listes, compteurs par objet (0x480 octets) |

Les numéros exacts des entrées des sous-systèmes autres que la table des objets sont déduits de la concordance entre les tailles dans le code et dans les fichiers. Ils restent à confirmer en lisant le chargement (`CRUSYS_1058_02f1`).

## Table des objets (entrée 0)

10 240 emplacements ; **le numéro d'emplacement est l'identifiant de l'objet**, celui que manipulent les scripts. Les données sont stockées en tableaux par champ, dans cet ordre :

| # | Champ | Octets | Tableau en mémoire | Vérification |
|---|---|---|---|---|
| 1 | x | 2 | `7DD9` | `Item_I_getX` |
| 2 | y | 2 | `7DDD` | `Item_I_getY` |
| 3 | z | 1 | `7DE1` | `Item_I_getZ` |
| 4 | forme (0 = emplacement libre) | 2 | `7DE5` | `Item_I_getShape` |
| 5 | image | 1 | `7DE9` | `Item_I_getFrame` |
| 6 | drapeaux (« statut ») | 2 | `7DED` | `Item_I_getStatus` |
| 7 | qualité | 2 | `7DF1` | `Item_I_getQ` |
| 8 | n° de PNJ | 1 | `7DF5` | `Item_I_getNpcNum` |
| 9 | « carte » | 1 | `7DF9` | `Item_I_getMapArray` |
| 10 | lien « suivant » | 2 | `7DFD` | chaînes de la grille, du contenu des conteneurs et de la pile en attente |
| 11 | tampon de travail | 2 | `7E01` | listes de résultats des recherches de collision (`ITEMFIND_10c8_18c0`, `FUN_1018_00fd`) ; sans signification durable |
| 12 | points de vie | 1 | `7E05` | posés par `Item_I_create` depuis `DAMAGE.FLX` ; valeurs 0, 1, 20, 50, 100… |

Ces douze tableaux totalisent 194 560 octets. L'entrée en fait 204 800 : les 10 240 derniers octets ne sont pas écrits par cette routine (non décodés).

Vérifié sur `SAVEGAME.1` et `SAVEGAME.13` :
- 8 288 et 8 301 emplacements occupés, **tous** avec une forme et une image valides ;
- l'emplacement 1 est le joueur (forme 1, PNJ n° 1). Son champ « carte » vaut la carte en cours (1 et 5).

## Listes d'objets

Le tableau 10 relie les objets en listes simplement chaînées. Un objet appartient à une seule liste :

- **Carte** : grille de 64 × 64 cases de 512 unités (`x >> 9`, `y >> 9`). Les têtes de chaîne sont dans l'entrée 1, juste après l'en-tête de 10 octets (offset 10, 4 096 mots, rangés par `x >> 9` puis `y >> 9`). Vérifié : sur `SAVEGAME.1`, les chaînes atteignent les 8 012 objets posés sur la carte, chacun dans la bonne case. La seconde grille (offset 10 + 0x2000) contient des compteurs par case.
- **Conteneur** (familles 6 et 7) : la chaîne commence au champ **qualité** du conteneur ; les objets contenus portent le drapeau `0x0008` et leur champ x contient l'identifiant du conteneur. Vérifié : 12 objets contenus sur 12 dans `SAVEGAME.1`, 49 sur 65 dans `SAVEGAME.13` (les 16 autres sont rattachés par leur champ x ; probablement portés par des PNJ).
- **Pile des objets en attente** (« ethereal void » de ScummVM, tête `7DD4`, compteur `7DD6`) : objets créés par `Item_I_create` ou retirés du monde par `Item_I_push`, marqués `0x0800`, en attente d'un `pop` (`popToCoords`, `popToContainer`, `popToEnd`, `pop`). Les identifiants 1 à 255 sont réservés aux PNJ ; `popToContainer` ne met jamais un PNJ dans un conteneur.

## Zone active (entrée 1, octets 2 à 9)

La zone active (« fast area ») est la partie de la carte où les objets sont vivants. Elle se compte en cases de la grille, avec `s = cx + cy` et `d = cx − cy` : `s0 ≤ s ≤ s1` et `d0 ≤ d ≤ d1`. C'est un losange sur la carte, donc un rectangle à l'écran. `ITEMCACH_10b0_0048` la centre sur la case de la caméra à `s ± 5`, `d ± 3`. Vérifié sur les 8 sauvegardes : largeur toujours 10 et 6, centrée sur la case du joueur ou à une case près (la caméra n'est pas exactement sur le joueur).

Quand la caméra bouge (`CameraProcess_I_moveTo` → `ITEMCACH_10b0_13be`) :
1. les objets des cases qui sortent de la zone la quittent (drapeau `0x2000` retiré, processus de l'objet arrêtés, événement usecode `0x10`) ; les objets jetables (`0x80`) sont détruits ;
2. dans les cases qui entrent, les œufs à glob de qualité non nulle sont développés : chaque objet est créé, posé, puis marqué `0x82` (jetable, issu d'un glob) ; il n'entre pas dans la zone (`10FC` est levé pendant l'expansion) ;
3. puis les autres objets de ces cases y entrent (drapeau `0x2000`, événement usecode `0x0F`), sauf à z = 254, les jetables, et les PNJ déjà dedans.

Les événements `0x0F` et `0x10` ne concernent que les PNJ et les formes dont l'octet 0 de `TYPEFLAG.DAT` a le bit 7.

Vérification : les objets jetables d'une sauvegarde sont tous dans sa zone active. En déplaçant la caméra de `SAVEGAME.1` jusqu'à la zone de `SAVEGAME.2`, le portage recrée exactement les 539 objets de glob (`0x82`) que l'original avait sauvegardés (même position, forme et image). Les objets jetables marqués `0x81` ont une autre origine.

`getX`, `getY` et `getZ` remontent la chaîne des conteneurs jusqu'à l'objet posé sur la carte ; `getPoint`, `getCX` et `getCY` lisent les champs de l'objet tels quels.

## Ce que contient la table

C'est **l'état complet de la carte en cours**. Sur `SAVEGAME.1` (carte 1) :
- 7 433 des 7 547 enregistrements de `FIXED.DAT` s'y trouvent à l'identique ; les autres ont été détruits ou déplacés en jeu ;
- 549 objets issus de globs y figurent : ceux qui étaient développés autour du joueur ;
- le reste correspond au joueur, aux PNJ et aux objets créés en jeu.

Charger une partie revient donc à remplacer la carte par cette table, puis à développer les œufs à glob sans recréer les objets déjà présents (`World::load_saved_items`, `World::expand_glob_eggs`). Les objets marqués `0x0008` sont rangés dans un conteneur et ne sont pas affichés.

**Objets « éthérés »** (drapeau `0x0800`) : hors du monde, posés à z = 254 (c'est le cas des 264 objets de `SAVEGAME.1` et des 1 143 de `SAVEGAME.13` qui portent ce drapeau). On y trouve notamment les personnages morts : le cadavre (forme 1211) reste au sol, tandis que le personnage lui-même (emplacement 1 à 255, numéro de PNJ = emplacement) est mis de côté avec les drapeaux `0x0400 | 0x0800 | 0x1000 | 0x8000`. Ces objets ne sont pas affichés.

## Table des PNJ (entrée 10)

Un enregistrement de 121 octets par emplacement 0 à 255 ; l'identifiant de l'objet PNJ est l'indice. Champs connus :

| Offset | Contenu | Lu par |
|---|---|---|
| `0x0C` | direction (0 à 15, 0 = vers −y, sens horaire) | `Actor_I_getDir` |
| `0x13` bit 0 | mort | `Actor_I_isDead` (vrai aussi pour un identifiant hors de 1..255) |
| `0x02`, `0x04`, `0x59` bit 3, `0x5A` bit 1 | remis à zéro à l'entrée dans la zone active | `Item_I_enterFastArea` |

Vérifié : sur `SAVEGAME.1` et `SAVEGAME.13`, les PNJ marqués morts sont exactement ceux mis hors du monde (drapeau `0x0800`) ; les directions vont de 0 à 15.

## À faire

- Décoder les 16 octets de fin de l'entrée 1 (têtes de la liste libre et de la pile en attente probables).
- Reste de l'entrée 10 (PNJ), entrée 20 (joueur), entrées du noyau : processus en cours, dont les scripts usecode avec leur pile.
