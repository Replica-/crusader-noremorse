# Nouvelle partie, cartes et téléportations

Comment une partie commence, comment le jeu passe d'une carte à l'autre et comment les œufs de téléportation déplacent le joueur. Code du portage : `Game::new_game` (`src/game/game.cpp`), `src/engine/world/map_change.cpp`, `teleport.*`, `inventory.*`.

## Nouvelle partie (`GAMEPAL_1028_0000`, `Game_start`)

`Game_start` affiche le menu principal (au démarrage) ou le reçoit de la touche Échap, puis traite son résultat : 0 = nouvelle partie, 1 = chargement, 2 = changement de carte, autre = quitter. Le bouton « nouvelle partie » ouvre d'abord l'écran de difficulté (voir `docs/interface.md`) ; le menu ne se ferme avec 0 que si un niveau est choisi.

Pour une nouvelle partie, dans l'ordre :
1. `KERNEL_11d8_11fe` termine tous les processus sauf ceux de tous les niveaux ; `DS:0907` = 1 (partie en cours) ; caméra non attachée.
2. Tables des objets et des PNJ remises à zéro (`ItemCache_free` / `_init`, `NpcTable_free` / `_init`). La liste libre des objets commence à l'identifiant 0x100 ; la carte en cours (`DS:10FE`) vaut −1.
3. Les globales usecode sont relues (`Flags_load(0)`) : voir plus bas.
4. Le joueur est refait (`NpcTable_init` puis `MainActor_reset`) :
   - objet 1, forme 1, image 0, PNJ n° 1, z 0, sur aucune carte ;
   - 150 points de vie (dossier PNJ +0), ni carte magnétique (`DS:7E1B`) ni batterie (`DS:7E1F`) ;
   - difficulté (`DS:7E19`) gardée si elle vaut 1 à 4, sinon 3 ; l'écran de difficulté l'a fixée juste avant.
5. Deux objets de départ (arguments lus dans l'assembleur, `1028:018E` et `1028:01EB`) :
   - le datalink (forme 0x4D4, image 0) est posé aux pieds du joueur puis ramassé par `MainActor_I_addItemCru(objet, 0)`. Il va dans l'emplacement d'objet 11 et devient l'objet actif ;
   - un conteneur (forme 0x598, famille 6, sans script) est mis dans le joueur (`popToContainer`, donc en tête de son contenu).
6. `CameraProcess_I_moveTo(0, 0, 0, 0)` (carte 0 = la carte en cours, donc rien ne se charge), caméra attachée au joueur, puis `MainActor_I_teleportToEgg(1, 0x1E, 1)`.
7. Musique arrêtée, barre d'état affichée, joueur hors stase.

La carte 1 ne se charge qu'au premier passage du noyau, quand le processus de téléportation s'exécute.

Les arguments de la ligne de commande d'origine (`DS:08F6` mission, `08F8`… position, `08FE` difficulté) permettent de démarrer ailleurs. La carte de départ de chaque mission est dans la table `DS:053C` : 0, 1, 3, 5, …, 0x1D, 0x28. Ces options ne sont pas reproduites.

## Globales usecode (`FLAG.C`, segment `1410`)

`Flags_load` lit les variables globales des scripts :
- pour une nouvelle partie, dans `usecode/unkds.dat` (chaîne `DS:65AC`) ;
- au chargement, dans l'entrée 60 de la sauvegarde.

Ce sont 0x42 octets dans les deux cas. `UNKDS.DAT` est nul sauf `+0x3C` = 1 et `+0x3E` = 2. Le fichier optionnel `usecode/unkdssym.dat` (`DS:65B6`, absent des données) contiendrait leurs noms. `Flags_save` écrit l'entrée 60.

Vérifié : l'entrée 60 de `SAVEGAME.1`, `.2` et `.13` fait 0x42 octets et reprend `UNKDS.DAT`, sauf les octets changés en jeu. `+0x3C` = 1 et `+0x3E` = 2 sont inchangés. `+0x0B`, `+0x1F`, `+0x3B` et `+0x40` (0x64) changent dans les trois ; une dizaine d'autres octets changent dans `SAVEGAME.13`, plus avancée.

## Carte en cours

La table des objets ne contient qu'**une carte** (`DS:10FE`). Changer de carte détruit ses objets ; revenir la relit depuis `FIXED.DAT`. Les PNJ (identifiants 1 à 255) restent dans la table d'une carte à l'autre.

**Déchargement** (`Map_unload`, `10B0:04C9`) — cases de la grille parcourues x à l'intérieur, y à l'extérieur, chacune dans l'ordre de sa chaîne. Chaque objet :
1. quitte la zone active, sans condition (`ITEM_10a8_2dad`) : ses processus sont arrêtés et l'événement 0x10 est lancé ;
2. puis, selon que c'est un PNJ ou non :
   - un PNJ est retiré de sa case. Il ne figure alors dans aucune liste (port : `Item::off_map`) et garde position et carte ;
   - un autre objet est détruit avec son contenu.

Les objets en attente et le contenu des PNJ restent.

**Chargement** (`Map_load`, `10B0:05D6`), pour une carte > 0 — les enregistrements de l'entrée `carte` de `FIXED.DAT` sont lus dans l'ordre :
- un enregistrement de forme 0 est sauté ;
- les conteneurs (familles 6 et 7) ont leur qualité remise à 0 : elle sert de tête à la chaîne de leur contenu ;
- les points de vie sont l'octet 5 de `DAMAGE.FLX`, sans tester le bit « présent » (`ItemData_from_record`) ;
- un objet posé prend le prochain identifiant libre et va en tête de sa case. Le bit 0 de ses drapeaux est effacé ;
- un objet contenu (drapeau 8) a pour x sa profondeur. Plus profond que le précédent : son conteneur est l'objet précédent ; moins profond : on remonte les conteneurs. Il va en fin de contenu, et y reprend la valeur de l'enregistrement ;
- un objet dont la forme a le bit 7 de l'octet 0 de `TYPEFLAG` reçoit l'événement usecode 4, « cachein » (`Item_cachein`, si la classe l'a).

Ensuite, du PNJ 255 au PNJ 1, chaque PNJ dont la carte est celle-ci revient en tête de sa case et reçoit aussi l'événement 4. Enfin `DS:10FE` prend la nouvelle carte.

**Caméra** (`CameraProcess_I_moveTo(x, y, z, carte)`, quatrième argument lu dans l'assembleur, `[BP+0xC]`, et utilisé par l'intrinsic 0x042 à 8 octets d'arguments) :
- carte 0 = la carte en cours ;
- une autre carte remplace la carte en cours, déchargée sauf si elle vaut 0 ;
- puis toute la nouvelle zone active entre (`FastArea_update`, `10B0:13BE`).

**`Actor_I_teleport(pnj, x, y, z, carte)`** (`10F0:29FA`, intrinsic 0x065) :
1. un PNJ de la carte en cours en sort de sa case, et quitte la zone active s'il change de carte ;
2. il prend ses nouvelles coordonnées et sa carte ; la liste des cibles (`TRACKER`) est vidée ;
3. si c'est le joueur et que la caméra le suit, la caméra y va et change donc de carte ;
4. s'il est sur la carte en cours, il revient dans sa case (`push` puis `pop`).

## Téléportation vers un œuf (`SHAPHAND.C`, segment `1098`)

**`MainActor_I_teleportToEgg(carte, œuf, attendre)`** (intrinsics 0x07A ; 0x097 sans carte = carte en cours) crée un processus (vtable `DS:0FE2`, `1098:011F`) :
- à la création :
  - les processus de type 0xF0 du joueur sont arrêtés ;
  - avec `attendre`, un second processus (`DS:1006`) lève l'indicateur « occupé » `DS:7E10` ; il attend la fin de la téléportation, puis le remet ;
  - tous les sons sont arrêtés.
- à son exécution (`1098:022D`) :
  1. sur une autre carte, le joueur est d'abord téléporté en (0, 0, 0) de cette carte, ce qui la charge ;
  2. la destination est le premier objet de la carte, case par case, qui est un œuf (famille 8) à l'image 1 dont l'octet bas de la qualité est le numéro d'œuf. Le critère est un script de recherche construit par `Scrit_build` (`1100:0000`) : `: %8 = \` %1 = & * %FF & %œuf = & $`. Toute la carte est parcourue (`AreaSearch_init` avec des bornes négatives) ;
  3. œuf trouvé : le joueur y est téléporté, et tombe (`Item_I_fall`) si moins de deux quarts opposés de son empreinte reposent sur quelque chose (`support_mask`, bit 0x10) ;
  4. pas d'œuf : le joueur retourne là où il était, sur son ancienne carte (`DS:0F89` empêche une autre téléportation pendant ce retour), et le jeu écrit « No destination for this egg! ».

**`MainActor_I_switchMap(carte)`** (intrinsic 0x0CC) crée un processus (`DS:0566`). À son exécution, il :
1. arrête tous les processus, lui compris, ainsi que les sons, la liste des cibles et les sprites (`Game_end_processes`) ;
2. téléporte le joueur sur l'œuf 0x1E de la carte.

**EggHatcher** (`1098:068B`, type 0x20F, appartient à l'œuf) — un œuf de famille 4, ou de famille 8 à l'image 0, en reçoit un quand il entre dans la zone active ; le processus s'arrête quand l'œuf en sort. À chaque passage (`1098:0718`), le joueur est « dans » l'œuf quand ces deux conditions sont remplies :
- son empreinte (x et y de `TYPEFLAG` × 16, sous son x et son y) chevauche le rectangle de demi-côtés 32 × (n° de PNJ de l'œuf >> 4) en x et 32 × (n° de PNJ & 0xF) en y autour de l'œuf. Les bornes sont calculées en 16 bits non signés ;
- son z est à moins de 0x30 de celui de l'œuf.

À l'entrée :
- un œuf de famille 8 téléporte vers l'œuf (qualité & 0xFF) de la carte (octet « carte » de l'œuf), en attendant si l'octet haut de la qualité est non nul (`1098:0072`, sans arrêter les sons) ;
- les autres œufs reçoivent l'événement 7 (« hatch »).

À la sortie, l'œuf reçoit l'événement 0x15.

C'est ce qui enchaîne le début de la mission 1. Le joueur arrive sur une dalle isolée de la carte 1 (œuf 0x1E en (6399, 6399, 0)). Un œuf de famille 8 à l'image 0 tout proche (qualité 0x63) l'envoie aussitôt sur le téléporteur d'arrivée (œuf 0x63 à l'image 1, en (30328, 29656, 16)).

## Inventaire du joueur (`MainActor_I_addItemCru`, `13E8:0330`)

L'équipement d'un PNJ est formé de 30 emplacements d'identifiants d'objets, dans son dossier (+0x1B). Le joueur les répartit ainsi :

| Emplacements | Contenu |
|---|---|
| 1 à 5 | armes (famille 10) |
| 6 à 10 | munitions (famille 11) |
| 11 à 25 | autres objets (familles 12 et 13, forme 0x4ED) |

L'arme active est à +0x15, l'objet actif à +0x19. Un objet équipé (`Actor_I_setEquip`) est en tête du contenu du PNJ, avec son emplacement en z (`Item_set_z`) et le drapeau 0x200.

`addItemCru(objet, afficher)` ramasse un objet et renvoie vrai s'il est pris. Le comportement dépend de l'objet :

| Objet | Effet |
|---|---|
| Forme 0x4ED | sa qualité s'ajoute à celle de l'objet porté de même forme ; sinon il prend un emplacement d'objet libre, image 0, et devient l'objet actif |
| Arme | refusée si une arme de même forme est portée ; sinon elle prend un emplacement d'arme, chargée d'un chargeur plein (ou de 0 pour une arme sans munitions), et devient active s'il n'y en avait pas |
| Munitions | un chargeur de plus sur celles portées (jusqu'à 20) ; sinon un emplacement libre, avec la qualité 1 |
| Carte magnétique 0x111 | ajoute le bit (qualité & 0xFF, s'il est < 32) des cartes magnétiques |
| Batteries 0x3A2 / 0x3A4 / 0x3A3 | portent la batterie à 1 / 2 / 3 et remplissent l'énergie ; une batterie déjà aussi bonne donne à la place 2500 / 5000 / 10000 d'énergie |
| Boucliers 0x52E à 0x530 | portent le niveau de bouclier (+0x66) à 1 / 2 / 3 ; si un bouclier est déjà porté, l'objet est simplement détruit |
| Autres objets | un de plus sur ceux portés (jusqu'à 10, 20 pour 0x560) ; sinon un emplacement d'objet libre, qualité 1, et objet actif |

Quand l'objet ramassé ne fait que s'ajouter à un autre, il est détruit. Un changement de qualité passe par `Item_I_setQAndCombine`, qui lance l'événement 0x0C (« combine »).

## Début de la mission 1 (`MISS1EGG`, classe 0x90D)

L'œuf de mission (famille 4, type 0x0D) se charge avec la carte 1 ; son script :
1. saut au noir (le cycle des couleurs se met en pause), joueur et commandes en stase (`setAvatarInStasis(1)`, `setCruStasis` : Échap n'ouvre plus le menu), bruitages arrêtés ;
2. `0x092` puis le briefing `01A.AVI`, puis `0x094` : l'écran reste noir et le cycle en pause à la fin de la vidéo ;
3. les objets de départ, créés et ramassés (`addItemCru`), et l'énergie du joueur (`setMana(2400)`) ;
4. `0x064` (vue redessinée), `fadeToGamePal(40)` attendu jusqu'au bout : son processus d'attente relance le cycle ;
5. fin de la stase (`clrCruStasis`), clavier et clics oubliés (`0x0D3`), destruction de l'œuf.

## Vérification

`game_tests` (`test_new_game`) vérifie :
- les globales de `SAVEGAME.13` (entrée 60) et celles de `UNKDS.DAT` après une nouvelle partie ;
- le joueur (forme 1, 150 points de vie, difficulté 3) et ses deux objets dans l'ordre d'origine ;
- au premier passage du noyau, la carte 1 : identifiants dans l'ordre du fichier à partir de 0x102, cases cohérentes, joueur sur l'œuf 0x1E ;
- une seconde plus tard, le joueur sur l'œuf 0x63 et l'indicateur « occupé » retombé ;
- le briefing joué, puis le fondu vers la palette du jeu : le cycle des couleurs reprend (en 43 images), la stase est levée, et aucune intrinsic manquante n'a été appelée ;
- `switchMap(3)` : carte 3 chargée sur son œuf 0x1E, objets de la carte 1 disparus, inventaire gardé ;
- `switchMap(2)` (sans œuf 0x1E) ramène le joueur sur la carte 1, comme l'original ;
- le chemin menu → difficulté → nouvelle partie.

Aucune erreur usecode. À l'écran (`crusader --new-game --frames 600 --png …`), on voit le début de la mission 1 : le téléporteur, un garde, les panneaux « Entrée interdite ».

## Non reproduit

- Rechargement des PNJ (carte 0, `ITEMCACH_10b0_0b13`) et options de démarrage de la ligne de commande.
- Un objet contenu marqué équipé (0x200) dans un PNJ n'est pas rééquipé au chargement. `FIXED.DAT` n'en a pas.
- Pendant un chargement de carte, l'original garde les bornes de l'ancienne zone active, si bien qu'un PNJ remis dans une case de cette zone y entre aussitôt. Le portage n'y fait rien entrer avant que la nouvelle zone soit calculée.
- `Kernel_fail_all` part de l'emplacement 3 de la table des processus ; les premiers emplacements du portage ne contiennent pas les mêmes processus.
- Préchargement des formes (`REFSTORE_10f8_0255`), compactage de la mémoire, cache des tuiles d'écran (`ANIMCACH_1188_1cb1`), boîte de débogage des œufs (`DS:0F88`).
- Objet ramassé montré à l'écran (`DISPLAY_1190_049c`, jamais pour le datalink).
- Lecteur de `DS:2172` (difficulté ≠ 1) non identifié.
