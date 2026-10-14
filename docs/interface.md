# Interface : polices, translucidité, barre d'état, curseur, menu, options

Ce document couvre ce que le jalon 3 a réimplémenté jusqu'ici. Code : `src/core/font.*`, `src/core/xform.*`, `src/core/dtable.*`, `src/engine/ui/` (barre d'état, curseur, codes de touche, menu). Noms des fonctions : `analysis/names_manual.tsv`.

## Polices (`CAFONT.C`, segment `1318`, et `HUGE.C`, segment `12C0`)

`STATIC/FONTS.FLX` contient une forme par police, au format 2D de `GUMPS.FLX` (en-tête d'image de 20 octets). L'image n d'une police est le glyphe du caractère de code n, lu **sans signe** (`MOV AH,0` avant l'appel au gestionnaire de formes). Le gestionnaire (`FontHandler_init`, cache de 32 formes) est créé par `CRUSYS.C`.

| Police | Images | Remarques |
|---|---|---|
| 0, 1 | 124, 122 | |
| 2 | 128 réelles | l'en-tête annonce 256 images ; les entrées 128 à 255 tombent dans les données des glyphes. À partir de l'image 58, le second champ de l'entrée contient la fin de l'image au lieu de sa longueur |
| 3 à 7, 12 à 17 | 58 à 256 | 8 à 11 absentes |

L'original ne vérifie ni le numéro d'image ni la longueur : `HANDLER_1070_026a` ne lit que le décalage. Le portage ignore donc la longueur au-delà de la fin de la forme, et un caractère sans glyphe valide (codes 128 à 255 de la police 2) n'affiche rien et n'avance pas, là où l'original lirait des octets quelconques.

**Objet police** (`Font_ctor`, 12 octets) : un flux texte `{vtable, x, y}` complété du numéro de police et de deux espacements tirés de tables de l'exécutable. Un numéro supérieur à 31 devient 0.

| Table | Rôle | Valeurs (polices 0 à 31) |
|---|---|---|
| `DS:4918` | ajouté à la largeur du glyphe (avance) | 1 pour les polices 0 et 4 à 17, 0 sinon |
| `DS:4958` | ajouté à la hauteur du glyphe (interligne) | 1 pour la police 0, 0 sinon |

**Écriture d'un caractère** (`TextStream_put`) : le glyphe est dessiné avec son point d'ancrage à la position du flux, puis :
- `'\n'` : y augmente de la hauteur du glyphe de `'\n'`, x revient au bord gauche du viewport courant (`DS:4098`, champ +6) ;
- `'\r'` : x revient à ce bord ;
- tout autre caractère : x augmente de son avance.

Les glyphes de `'\n'` et `'\r'` sont dessinés comme les autres.

**Fonctions du flux**
- `TextStream_printf_at` (`12C0:02D7`) : `vsprintf` dans le tampon `DS:4626`, puis écriture caractère par caractère à partir de (x, y).
- `TextStream_puts_clipped` (`12C0:0183`) : s'arrête avant le premier caractère dont l'avance dépasserait « x de départ + largeur » ; renvoie le nombre de caractères écrits.
- `TextStream_measure` (`12C0:01F9`), d'après l'assembleur (le pseudo-C est faux) : la largeur est celle de la plus longue ligne ; un `'\n'` termine la ligne, et **son avance compte dans la ligne suivante**. La hauteur additionne la hauteur du glyphe à chaque `'\n'`, plus la hauteur du dernier caractère.
- `Font_baseline` : ancrage vertical du glyphe 0.

Vérifié (`core_tests`) : 2 277 glyphes décodés, toutes les lignes RLE font exactement la largeur de l'image, et seules les 128 entrées prévues de la police 2 sont illisibles.

## Translucidité (`XFORMPAL.DAT`, blitter n° 4)

`GAMEPAL.C` (`XformPal_load`, `1030:0250`) lit l'entrée 0 de `STATIC/XFORMPAL.DAT` (une Flex, 2 304 octets) et passe aux blitters un pointeur situé 256 octets après le début : appelons-le T.

- `T[c]` (256 octets) : pour chaque couleur, le numéro de table de mélange `d` (1 à 7), ou `0xFF` si la couleur est opaque. Seules les couleurs **8 à 14** sont translucides (tables 1 à 7).
- `T[d × 256 + fond]` : la couleur qui remplace le pixel `fond` sous la couleur translucide `d`.

Les 256 premiers octets de l'entrée et l'entrée 1 (32 octets) ne servent pas au dessin.

Le blitter translucide (`asm_blit_4`) fait partie des blitters 32 bits que `asm_blitters_install` recopie en mémoire depuis le segment `1000` (décalages `0x0F88` à `0x14EB` pour celui-ci). Ghidra les désassemble en 16 bits ; il faut les relire en 32 bits (capstone). Pour chaque pixel source `s` : `d = T[s]` ; si `d == 0xFF`, on écrit `s`, sinon `T[d × 256 + pixel de destination]`. Le reste du blitter n'est que le découpage aux bords, identique au blitter opaque. Le portage l'applique dans `ShapeFrame::draw` quand on lui passe la table.

À faire : les formes du monde marquées translucides (`TYPEFLAG.DAT`, drapeau `0x0800`) passent sans doute par le même blitter ; à vérifier dans `SPRITE` / `DISPLAY` avant de l'activer.

## Formes d'interface (`GUMPSHAP.C`)

`GumpShape_draw(x, y, forme, image, translucide)` dessine une image de `GUMPS.FLX` avec son point d'ancrage en (x, y), avec le blitter opaque ou le blitter translucide (table de `DS:0590`). `GumpShape_rect` renvoie `{0, 0, largeur − 1, hauteur − 1}`.

## Barre d'état (segment `11B8`, « CruStatusGump » de ScummVM)

Cinq cases translucides en bas de l'écran. Le drapeau `DS:2DAC` la montre ou la cache : intrinsics `0x061` (`I_showStatusGump`) et `0x05F` (`I_hideStatusGump`). Le démarrage d'une partie (`Game_start`, `1028:0000`), nouvelle ou chargée, se termine par son affichage. En la montrant, l'original limite aussi la souris à y ≤ 439 et la place en (320, 240) ; en la cachant, il rend la limite à 479.

**Disposition** (`StatusBar_init`) : la case i est l'image `{0, 1, 0, 2, 3}[i]` (table `DS:2DAD`) de la forme 1 de `GUMPS.FLX`, 108 × 28 pixels, coin haut gauche en **x = 15 + 127 i, y = 450**. Le code voulait espacer les cases de (640 − largeur) / 5, mais il lit la largeur avant d'avoir rempli le rectangle de la case, encore à zéro : la largeur vaut 1, l'écart 639 / 5 = 127 et la marge 127 / 8 = 15 (vérifié à l'assembleur, `11B8:004C`).

**Dessin** (`StatusBar_draw`), case par case :

| Case | Fond (translucide) | Contenu |
|---|---|---|
| 0, arme | si le joueur est le PNJ contrôlé et a une arme active | icône de l'arme (forme 3) au centre |
| 1, munitions | idem, et l'arme utilise des munitions | police 15 : qualité de l'arme (balles chargées) en (+30, +11) ; en (+75, +11), qualité du premier objet de l'équipement du joueur qui est cette munition, sinon `0` |
| 2, objet | si le joueur est contrôlé | icône de l'objet actif au centre ; sa qualité en police 12 en (+75, +18) si elle dépasse 1 |
| 3, santé | toujours | barre de couleur `0x41` de (+34, +7) à (+34 + w, +20) inclus, w = (hp × 66 sur 16 bits) / hp max, si hp ≠ 0 |
| 4, énergie | si le joueur est contrôlé | barre de couleur `0xF5`, w = énergie × 66 / 10 000, si l'énergie ≠ 0 |

Sauf pour la case 3, rien n'est dessiné dans une case si le joueur n'est pas le PNJ contrôlé. Le centre d'une case est `((x1 − x0 + 1) >> 1) + x0`. Les nombres sont écrits avec `"%d"` (`DS:2DB8`) ; `"0"` est la chaîne `DS:2DBB`.

**Sources**
- Arme et objet viennent du joueur. `DS:7E0E` contient son identifiant : la variable n'est écrite nulle part sous ce nom, et `Player_find_equipped_shape` lit en dur l'équipement du PNJ 1 ; le portage utilise 1. Santé et énergie viennent du PNJ contrôlé (`World_I_getControlledNPCNum`).
- Fiche du PNJ (0x79 octets) : +0x00 points de vie, +0x15 emplacement de l'arme active, +0x19 emplacement de l'objet actif (0 = aucun), +0x1B équipement (30 mots, identifiants d'objets), +0x57 énergie.
- Icône de l'objet : forme 3 image 13 pour les crédits (forme `0x4ED`, « CREDITS » dans la liste des objets ramassés) ; forme 4 pour la famille 12 ; forme 5 sinon, où les icônes 3, 5 et 7 deviennent 18, 17 et 19 si l'image de l'objet n'est pas 0.

**Tables `DTABLE.C`**, données statiques de l'exécutable sauf la dernière :

| Table | Enregistrements | Champs lus |
|---|---|---|
| armes, `DS:215A` | 21 × 0x12 octets | +0 forme, +6 type de munitions, +0xD (non aligné) icône |
| munitions, `DS:20D6` | 8 octets par type | +0 forme : types 1 à 5 → formes 829 à 833 |
| objets, `DS:22D8` | 12 × 4 octets | +0 forme, +2 icône (forme 5) |
| objets de famille 12, `DS:2308` | 9 × 8 octets | +0 forme, +6 icône (forme 4) |
| types de PNJ, `DTABLE.FLX` entrée 0 | 64 × 0x8E octets | +2 points de vie maximum, +0x3E forme (joueur : 150) |

Une recherche infructueuse renvoie l'enregistrement 0 (tout à zéro), sauf celle des types de PNJ qui renvoie −1 ; l'original lirait alors avant la table, le portage dessine une barre vide.

**Mise à jour** : l'original ne redessine que les cases touchées par les rectangles modifiés de l'image (`StatusBar_mark_dirty`, appelée par les scripts, le combat, l'inventaire) et sauvegarde le décor sous chaque case pour le restaurer avant l'image suivante (`StatusBar_restore_background`). Le portage redessine toute la barre sur chaque image, ce qui donne le même résultat à l'écran.

Vérifié (`game_tests`) sur `SAVEGAME.13` : arme 818 (1 balle), munitions 830 × 16, objet 1376 × 16, santé 141 / 150 → barre de 62 pixels, énergie 130 → barre d'un pixel ; et les cinq rectangles.

## Objets ramassés (`DISPLAY.C`, segment `1190`, « PickupAreaGump » de ScummVM)

En haut à gauche de l'écran, la liste des objets que le joueur vient de ramasser : une bande par objet, avec son icône, son nom et leur nombre. Code : `src/engine/world/pickup_list.*` (la liste, dans `GameState::pickups`) et `src/engine/ui/pickup_area.*` (le dessin).

**La liste** : 8 entrées de 0x19 octets en `DS:8346`, vidées au lancement du jeu (`PickupList_init`, appelée par `CRUSYS`) :

| Champ | Contenu |
|---|---|
| +0 | nom (pointeur lointain) |
| +4 | icône : {image, forme, 0} |
| +8 | nombre |
| +0xA, +0xC | forme et image de l'icône (la clé de l'entrée) |
| +0xE | secondes restantes |
| +0x10 | à redessiner |
| +0x11 | rectangle de la bande |

**Ajout** (`PickupList_add`, `1190:049C`) : `MainActor::I_addItemCru(objet, montrer)` l'appelle avec l'objet, avant de le détruire, quand `montrer` est vrai. C'est le cas des contenus de coffres et de cadavres (scripts `FREE`) et du ramassage par la sélection. `PickupList_describe` (`1190:070F`) donne l'icône (`GUMPS.FLX`) et le nom :

| Objet | Icône | Nom |
|---|---|---|
| passe `0x111` | forme 5, image 13 + image de l'objet | `PASSE` |
| crédits `0x4ED` | forme 3, image 13 | `CREDITS` |
| famille 10 (armes) | forme 3, icône de l'arme | table `DS:2350` (`BA-40`…) |
| famille 11 (munitions) | forme 5, mot 3 de l'enregistrement de munitions | table `DS:23B8` (`CHARGEUR BA-41`…) |
| famille 12 | forme 4 | table `DS:23D0` (`GRENADE`, `MINE`…) |
| famille 13 | forme 5 | table `DS:2388` (`MEDIKIT`, `DATALINK`…) |

Tout autre objet arrête l'original sur une assertion (`DISPLAY.C`, ligne 0x136) ; `addItemCru` n'en passe jamais. Les noms sont ceux de la version française, chaînes `DS:2420` à `DS:2598` ; les tables de noms n'ont que 14 armes pour 21 enregistrements d'armes, les 7 derniers ayant la forme 0 et n'étant jamais trouvés.

- Si une entrée a déjà la même icône, son nombre augmente de 1 et elle reste 4 secondes de plus. Le test des crédits compare la forme de l'icône (3), et non celle de l'objet, à `0x4ED` : il n'est jamais vrai, et des crédits ramassés pendant que leur entrée est affichée comptent 1 chacun.
- Sinon, la première entrée libre : nombre 1, ou pour des crédits leur qualité (1 si elle vaut 0), 4 secondes. Rien quand les 8 sont prises.
- Le datalink `0x4D4` n'est jamais montré quand il devient un nouvel objet ou s'ajoute à ceux portés.
- Un bouclier (`0x52E` à `0x530`) est montré après sa destruction : l'original lit alors la forme d'un objet détruit. Le portage le montre juste avant.

**Durée** : un processus (`PickupList_process_ctor`, à tous les niveaux) compte les secondes. Dès que 60 ticks sont passés depuis son dernier compte (`PickupList_process_run`), chaque entrée perd une seconde et se libère à 0 (`PickupList_count_second`). Une entrée reste donc entre 3 et 4 secondes.

**Dessin** (`PickupList_draw`, `1190:010F`), après la barre d'état, sauf en mode menu (`DS:2CEE`, mis avec `DS:2D8F` par `World_enter_menu_mode`). L'entrée i est une bande de 28 pixels de haut en x = 4, y = 32 i + 4 :
- largeur W : largeur de l'icône − 1, plus largeur du nom en police 13, plus 15, arrondi au multiple de 28 inférieur, plus 56 ;
- fond : forme 2 image 0 de `GUMPS.FLX` (30 × 30), translucide, tous les 28 pixels tant qu'on est sous W ;
- l'icône, opaque, en (+33, +14) ;
- le nombre en police 12 en (+53, +20) s'il dépasse 1 (`"%d"`, `DS:2CEF`) ;
- le nom en police 13 en (+66, +10).

**Non reproduit** : l'original ne redessine une entrée que si elle a changé ou si un rectangle modifié de l'image la touche. Il sauvegarde le décor sous chaque entrée (8 zones de 199 × 27, `PickupList_init`) pour le remettre ensuite (`PickupList_restore`). Le portage dessine toutes les entrées sur chaque image.

Vérifié (`game_tests`, `SAVEGAME.1`) :
- le coffre 3169 montre `CHARGEUR SG-A1` (forme 5 image 10), qui disparaît dans les 4 secondes ;
- le coffre 3160 montre `CHARGEUR BA-41` ×2 sur une seule entrée ;
- le cadavre 8389 montre `CREDITS` ×5.

Rendu contrôlé avec `crusader data --save 1 --use 3160 --frames 60 --png …`.

## Écran des ordinateurs (`BARK.C`, segment `13A0`, « ComputerGump » de ScummVM)

Les moniteurs (classes `MONITNS` / `MONITEW`, qui passent par `TEXTFILE`) et le tableau de chasse (`VMAIL`) affichent un texte sur l'écran d'un ordinateur, caractère par caractère. Code : `src/engine/ui/computer_gump.*`, ouvert par `Game::read_computer`.

**Ouverture** : l'intrinsic `ComputerGump::I_readComputer` (0x0FE, `13A0:0005`) reçoit le texte, passe en mode menu (`World_enter_menu_mode`), fait tourner le gump par `Gump_run_modal` puis quitte le mode menu. Le jeu est donc figé, et le script reprend après la fermeture. Les scripts composent souvent le texte avec `UCMachine::I_numToStr` (0x0FF, 0x113 et 0x122, `1418:0073`) : `itoa(valeur, tampon, 10)` sur un entier signé, dans une nouvelle chaîne.

**Texte** (`ComputerGump_set_text`, `13A0:03C8`) : une copie, coupée à 20 colonnes. `^` est un saut de ligne. En parcourant le texte, l'original compte les caractères depuis le dernier saut (le code 1 ne compte pas) et retient le dernier espace vu. Sur un caractère où le compte atteint 20 et où un espace a été vu, cet espace devient `^` et le compte repart à (position − espace). À l'écran, le code 1 et `*` n'occupent pas de colonne.

**Mise en page** (`ComputerGump_ctor`, `13A0:0034`, et `ComputerGump_layout`, `13A0:0487`) :
- forme `0x1E` de `GUMPS.FLX` : image 0 (le haut du moniteur) en (x, y), image 1 (le bas) en (x, y + 160) ;
- le gump, de la largeur de l'image 0 et de la hauteur des deux, est centré sur l'écran ;
- les caractères en police 6, sur une grille de 8 × 9 pixels à partir de (x + 41, y + 38), 20 colonnes ;
- quand la ligne 13 (la 14ᵉ) est atteinte, la page est pleine : `-SUITE-` (`DS:55AB`) s'affiche au début de cette ligne ;
- le curseur est le glyphe `0x5B` de la police 6, à la place du prochain caractère.

Champs : texte `+0x34`, caractères montrés `+0x38`, début de page `+0x3C`, colonne et ligne `+0x46` / `+0x48`, page pleine `+0x3B`, saut `+0x4A`. `DS:55AA` demande un dessin complet.

**À chaque passage de la boucle modale** (`ComputerGump_run`, vtable +4, `13A0:08D2`) :
- texte fini : un simple dessin ;
- saut demandé : la mise en page est refaite, sans dessin, jusqu'à la page pleine ou la fin du texte, puis le saut est annulé et un dessin complet demandé ;
- un dessin (`ComputerGump_draw`, `13A0:02D1`), qui montre un caractère de plus et le curseur à la position suivante, sauf si la page est pleine ou le texte fini ;
- si la page n'est pas pleine : après un `*`, une pause ; sinon le son `0x33` relancé (sauf après un saut de ligne, code 10) et un tick d'attente (`SystemTimer_busy_wait`).

Le texte avance donc d'un caractère par tick (60 par seconde).

**La pause après `*`** : `1470:0692` est le `delay(ms)` de la bibliothèque C, appelé avec 4800. Il mesure le temps avec le compteur du PIT, comme si ce compteur faisait un tour en 65536 coups (`DS:77CC` = 1193 coups par milliseconde). Or le jeu reprogramme le PIT (mode 3, `11D8:2A70`) au rythme de son processus le plus rapide, 60 Hz : le compteur fait alors un tour en 19886 / 2 coups, et la pause dure environ 4800 × 9943 / 65536 ≈ 728 ms. Le portage attend 44 ticks.

**Commandes** :
- touches (`ComputerGump_key`, vtable +0x18, `13A0:0A2A`) : sur une page pleine, n'importe quelle touche passe à la page suivante. Sinon, Échap ou Entrée ferment le gump une fois le texte fini, et Échap saute jusqu'à la fin de la page avant cela ;
- clic (`ComputerGump_mouse`, vtable +8, `13A0:0887`) : un appui ferme le gump une fois le texte fini, et demande le saut sinon. Sur une page pleine, il ne fait donc rien : seules les touches tournent les pages ;
- le curseur de la souris est caché tant que le gump est ouvert. À la fermeture (`ComputerGump_dtor`, `13A0:0212`), il revient et le clavier est vidé.

**Non reproduit** :
- les dessins partiels. L'original ne dessine que le nouveau caractère, sur une case d'abord remplie de la couleur 0 (de la taille du curseur pour un `^`), et ne redessine toute la page que sur `DS:55AA`. Le portage dessine la page sur chaque image ; l'écran du moniteur étant noir, les cases remplies ne se voient pas ;
- un dessin du gump hors de sa boucle (à l'ouverture, par exemple) montre aussi un caractère de plus dans l'original ;
- le joystick (`DS:6396`) : le gump attend que ses boutons soient relâchés, et un bouton envoie Échap ;
- la pause après `*` bloque toute la boucle de l'original, événements compris. Le portage ne fait que retarder le caractère suivant ;
- `DS:0636` (animations arrêtées) et `DS:0907` mis à 0 tant que le gump est ouvert ; à la fermeture, la vue est redessinée (`DS:2C62` +0x2C, +0) et les clics oubliés (`DS:8A80..8A84`).

Vérifié (`game_tests`, `SAVEGAME.2`) : le moniteur 5449 (`MONITEW`) ouvre l'écran avec le message de DBEYER, dont le code d'accès « 962 » vient de `numToStr`. Un caractère s'affiche par tick. Échap remplit la page, une touche passe à la suivante, et Échap ferme l'écran à la fin du texte, puis la barre d'état revient. Aucune intrinsic manquante.

Rendu contrôlé avec `crusader data --save 2 --use 5449 --frames 120 --png …`, et `--frames 320` pour la page pleine avec `-SUITE-`.

## Clavier à code (`IRGUMP`, segment `13C8`, « KeypadGump » de ScummVM)

Les portes à code ont un clavier (classe `KEYPAD`). Code : `src/engine/ui/keypad_gump.*`, ouvert par `Game::show_keypad`.

**Script** (`KEYPAD`, événement 0x20, l'utilisation) :
- il cherche dans un rayon de 3200 un `VALBOX` (forme `0x251`) de même `QLo`, et lui demande le code (fonction 32). Au-delà de 999, il s'arrête là ;
- si le clavier n'est pas déjà occupé (drapeau `0x1000`), il pose ce drapeau et ouvre le clavier avec le code ;
- résultat -1 (annulé) : il enlève le drapeau, rien d'autre ;
- résultat égal au code : le son 122, puis `TRIGGER` ouvre la porte, et le drapeau 4 est posé ;
- autre résultat : `TRIGGER` avec 1, puis l'alarme si elle n'est pas déjà active.

**Ouverture** : l'intrinsic `KeypadGump::I_showKeypad` (0x0C5, `13C8:00FE`) reçoit le code, passe en mode menu (`World_enter_menu_mode`, curseur visible), fait tourner le gump par `Gump_run_modal`, quitte le mode menu et renvoie le résultat du gump au script. Dans le portage, l'intrinsic ouvre l'écran et renvoie 0 ; le processus du script est figé avec le jeu, et son registre de résultat reçoit la valeur à la fermeture, avant qu'il ne reprenne (`UCProcess::set_call_result`).

**Mise en page** (`KeypadGump_ctor`, `13C8:0133`) :
- forme 10 de `GUMPS.FLX`, image 0, centrée sur l'écran ;
- douze boutons enfants (`KeypadButton_ctor`, `13C8:0000`), forme 11, image i (i + 12 enfoncé, `KeypadButton_select`, `13C8:006D`), chacun sur un rectangle du gump (bornes comprises) : colonnes 12..38, 39..65, 66..91, lignes 25..49, 50..73, 74..97, 98..123 ;
- numérotés de gauche à droite puis de haut en bas : 0..8 les chiffres 1..9, 9 l'effacement, 10 le chiffre 0, 11 la validation ;
- les chiffres saisis (`KeypadGump_draw`, `13C8:08F5`) en forme 12, image chiffre − 1 (9 pour le 0), à partir de (x + 25, y + 12), chacun à la largeur de l'image + 2 du précédent.

Champs : chiffres saisis `+0x39` (8 au plus), valeur `+0x3B` (32 bits), code `+0x3F`, réussite `+0x43` (0xFF tant que rien n'est vérifié), chiffres `+0x44`. `DS:0636` (animations arrêtées) est posé tant que le clavier est ouvert.

**Boutons** (`Button_on_mouse_deselect`, `1310:0254`) : un appui dans un bouton capture la souris et l'enfonce. Un glissement (`1310:0151`) le renfonce dedans et le relâche dehors. Le relâchement termine la capture, prévient le gump si la souris est dans le bouton, puis relâche le bouton. Un bouton relâché alors qu'il était enfoncé joue le son `0x3A` (effacement) ou `0x3B` (`KeypadButton_sound`, `13C8:0086`).

**Saisie** (`KeypadGump_on_button`, `13C8:0543`, et `KeypadGump_on_key`, `13C8:0646`) :
- un chiffre (bouton, ou touche `0`..`9`) s'ajoute s'il y en a moins de 8. La touche joue aussi `0x3B`, seulement quand le chiffre est ajouté ;
- l'effacement retire le dernier chiffre ; la touche Retour arrière joue aussi `0x3A`, même sans chiffre ;
- la validation (le bouton toujours, Entrée seulement avec au moins un chiffre) calcule la valeur (`13C8:089A`), la vérifie (`Keypad_check_code`, `13C8:09DB`), l'envoie comme résultat et ferme le clavier ;
- Échap envoie -1 et ferme le clavier ;
- les symboles des chiffres avec Maj sur un clavier américain comptent comme des chiffres : `! @ # $ % ^ & * ( )` pour 1..9 et 0.

**Vérification**, seulement si le code est non nul : la valeur 74697689 (`0x0473CBD9`) est un code de triche, prise comme le code. Sinon la valeur est réduite modulo 10000 (`rtl_lmod`, `1000:339D`). Son `0x32` si elle égale le code, `0x31` sinon. Le résultat est la valeur, tronquée à 16 bits.

**Non reproduit** :
- le résultat passe par un événement de gump de type 0x10 (`Gump_send_event`, `1368:1297`), dont `Gump_run_modal` renvoie la valeur sur 16 bits ; le portage garde directement les 16 bits bas ;
- `DS:0636` ; le vidage du clavier et des clics (`DS:8A80..8A84`) du destructeur (`13C8:04EE`) est fait par le jeu à la fermeture ;
- un double-clic (événement de type 4) compte comme un appui.

Vérifié (`game_tests`, `SAVEGAME.2`) : le clavier 5459 s'ouvre avec le code 962 (celui que donne le moniteur 5449), curseur visible et barre d'état cachée. Saisie au clavier, Maj+1 compris, et effacement. Échap : le script enlève seulement son drapeau `0x1000`. Bouton 1 puis validation à la souris : le son `0x31`, et le drapeau 4 n'est pas posé. `962` puis Entrée : le son `0x32`, et le drapeau 4 est posé. Aucune intrinsic manquante.

Rendu contrôlé avec `crusader data --save 2 --use 5459 --frames 10 --png …`.

## Curseur de la souris (`MOUSECTL.C`, segment `1360` ; `U9MOUSE.C`, segment `13D8`)

Le curseur est dessiné par le jeu à partir de `STATIC/MOUSE.SHP` (une forme seule, 40 images au format 2D), avec le point d'ancrage de l'image sur la position de la souris. L'original sauvegarde l'écran sous le curseur pour le restaurer ; le portage le dessine en dernier sur chaque image.

**Pile d'images** (`+0x186`, 8 niveaux, niveau courant `+0x196`) : `cursor_push` monte d'un niveau (sans dépasser 7), `cursor_pop` descend (sans passer sous 0) et renvoie l'image quittée, `cursor_reset` revient au niveau 0 ; tous écrivent l'image du niveau courant.

**Curseur directionnel** (`game_mouse_cursor_frame`, hérité d'Ultima 8) : les entrées 0 et 0x19 de la pile ne sont pas des images. On calcule la direction 0 à 7 (0 = haut, sens horaire) de la souris vue du point (320, 200), fixé à la création et jamais modifié : avec dx, dy l'écart et r = dy × 8 / dx (division tronquée), r de −11 à −3 donne 1 ou 5 selon le signe de dx, |r| ≥ 12 donne 0 ou 4 selon dy, r de 3 à 11 donne 3 ou 7, r de −2 à 2 donne 2 ou 6, et dx = 0 donne 0 ou 4. L'image vaut direction + entrée, plus 8 si l'entrée est 0 et que la souris est hors du rectangle (320 ± 30, 200 ± 25) ou à moins de 20 pixels d'un bord (« courir »). Le résultat est borné à la dernière image.

**Images utiles** : 0x23 attente, 0x24 flèche, 0x25 flèche des menus. Les images 0 à 7 de `MOUSE.SHP` sont huit copies d'un même dessin. Crusader ne montre le curseur que dans les menus ; en jeu, il reste caché (`WORLD_13e8_0ef9`).

**Plage du pilote** : x de 0 à 639, y de 0 à 479, ou 439 tant que la barre d'état est affichée ; l'afficher ou la cacher replace la souris en (320, 240).

## Codes de touche des gumps (`DISPATCH_1328_0624`)

Un événement du clavier devient un code, selon les bits 0 à 3 du mot d'état BIOS (Maj, Ctrl, Alt) : sans modificateur, le code ASCII, ou 0x100 + code de touche s'il est nul ; Alt : 0x200 + code de touche ; Ctrl : 0x400 + code de touche (prioritaire sur Alt) ; Maj : l'ASCII, ou 0x800 + code de touche (prioritaire sur les deux). Exemples : Échap 0x1B, flèche haut 0x148, Alt+X 0x22D. Le portage a ajouté au mot d'état les bits 0x04 (Ctrl) et 0x08 (Alt) du BIOS, qui lui manquaient.

## Commandes du joueur en jeu (`World_on_key`, `13E8:14B4`)

La vue du jeu reçoit les codes de touche ci-dessus. Le portage reproduit Échap (menu, voir plus bas), les touches d'inventaire (`src/engine/world/player_commands.cpp`), le mode de sélection (`item_selection.cpp`) et les touches de fonction (voir plus bas) :

| Touche | Effet | Fonction |
|---|---|---|
| Q / q | arme précédente | `Player_previous_weapon` (`13E8:0C07`) |
| W / w, `*` du pavé | arme suivante | `Player_next_weapon` (`13E8:0C72`) |
| I / i | objet précédent | `Player_previous_item` (`13E8:0D5F`) |
| O / o, `−` du pavé | objet suivant | `Player_next_item` (`13E8:0DC0`) |
| U / u, Suppr du pavé | utiliser l'objet actif | |
| M / m | utiliser un médikit (forme 0x351, classe MEDIKIT) | |
| E / e | utiliser un cube d'énergie (forme 0x582, classe ENERGY) | |
| B / b | déclencher les charges DETPAC (forme 0x55F) proches | |
| Ctrl+D (code 0x420) | lâcher l'arme active | |
| S / s, `+` du pavé | mode de sélection : choisir un objet proche | `ItemSelection_start` (`13E8:33D9`) |
| G / g | ramasser tous les objets proches | `ItemSelection_start` |
| R / r | désignation de cible active ou non, annoncé dans la boîte de message | |

Deux touches voisines passent par le Controller (`1138:220A` et `1138:0896`, voir `docs/combat.md`) : Tab et J (saut) et Z (la caméra revient sur le PNJ contrôlé, vivant, sauf le joueur en stase ; le mode de sélection se termine).

**Condition commune** : le joueur n'est pas en stase (`DS:7E10`, lu et écrit par `Ultima8Engine::I_get/setAvatarInStasis`, levé aussi par les bornes de soin et de recharge et par une téléportation avec attente), il est vivant et c'est le PNJ contrôlé. M et E demandent en plus que la « stase Crusader » (`DS:5FFB`) soit levée. Les scripts de mission tiennent le joueur en stase pendant leurs scènes : au début de la mission 1, la scène d'arrivée (classe 0xA11, événement 0x23, lancée par MISS1EGG) le libère une quinzaine d'images après le retour des couleurs.

**Emplacements** : l'équipement du joueur compte 30 emplacements ; les armes occupent 1 à 5, les objets 11 à 25 (voir `inventory.hpp`). L'emplacement de l'arme active est à +0x15 de l'enregistrement du PNJ, celui de l'objet actif à +0x19.

**Changer d'arme ou d'objet** : à partir de l'emplacement actif (s'il vaut 0, rien ne se passe), le premier emplacement occupé dans le sens choisi, en bouclant ; l'emplacement actif est essayé en dernier. Vers le haut, les objets bouclent après l'emplacement 24 (0x18) : le 25 n'est atteint qu'en descendant, une asymétrie de l'original reproduite telle quelle. W exige en plus que le PNJ contrôlé soit le joueur (`DS:2852` = 1). Il remet le joueur dans la pose de la nouvelle arme (`Actor_set_pose`, `1138:32A8`), sauf s'il est occupé par une animation :
- à genoux : action 0x2E ;
- sinon, si sa dernière action n'est pas une action de repos (0, 1, 0xB, 0x10, 0x20, 0x37, 0x3A, 0x3B, `1138:0014`) : action 4, la garde ;
- avec une arme dont l'enregistrement ne garde pas les actions (`Actor_weapon_keeps_actions`), 0x2F et 0x25 à la place, comme dans `remap_action`.

**Utiliser** (`Item_I_use`, `10A8:2667`, aussi intrinsics 0x081, 0x0D0 et 0x0D5) : lance l'événement 1 (« use ») de la classe de l'objet, si elle l'a ; refusé pour un PNJ mort, et pour un PNJ en combat autre que le joueur. U ne fait rien pour la forme 0x4ED. Après l'utilisation, l'objet est consommé, sauf le datalink 0x4D4, le scanner 0x52D et les boucliers 0x52E à 0x530 : sa qualité baisse de 1 (`Item_I_setQAndCombine`), et à 0 il est détruit, quitte ses emplacements (`Actor_unequip`, `10F0:2933`), puis l'objet suivant devient actif. M et E consomment de même le premier médikit ou cube porté. Mais l'objet suivant n'y devient jamais actif : l'original compare l'objet actif à l'objet consommé après avoir vidé son emplacement, donc la comparaison échoue toujours. Le portage garde ce comportement.

Le travail est fait par le script de l'objet, qui tourne après la consommation : le médikit rend 80 points de vie (`Actor::I_addHp`, au plus 150 pour le joueur). Si le joueur les a déjà tous, `I_addHp` renvoie 0 et le script recrée un médikit qu'il rend au joueur (`MainActor::I_addItemCru`).

**Déclencher les DETPAC** (B) : `Item_I_use` sur chaque objet de forme 0x55F du carré de ±0x800 unités autour du joueur (`AreaSearch_init` avec le script de recherche `@ %0x55F = $` ; la recherche porte sur les objets posés sur la carte).

**Lâcher l'arme** (Ctrl+D) : le joueur doit être le PNJ contrôlé (`Item_is_controlled_npc`, `10A8:00C1`). L'arme 0x32E, la première de la table des armes, est gardée sous la difficulté 2. Sinon, l'arme suivante devient active (comme W, avec sa pose), puis l'arme quitte le joueur et est posée à ses coordonnées. Un objet équipé qui sort de son conteneur quitte toujours ses emplacements d'équipement (drapeau 0x200, `Item_push_pending`, `10B0:1C21`).

**Mode de sélection** (S et G ; drapeau 0x100 de la vue du jeu et `DS:5FF7`). Il démarre si le joueur peut agir, hors « stase Crusader » et si la vue n'est dans aucun autre mode (`+0x5B` = 0) ; l'indicateur « téléportation Crusader » (`DS:6000`, intrinsics 0x071 / 0x072 / 0x075) est d'abord baissé.
- **Liste** (`ItemSelection_start`) : parmi les objets 0x100 à 0x27FF, ceux qui ne sont ni garés (z 254), ni rangés (état 8), ni marqués 0x1000, de forme 0x4ED, des familles 10 à 13 (armes, munitions, objets d'inventaire) ou « sélectionnables » (`TYPEFLAG`, octet 6 bit 1 : interrupteurs, ordinateurs…). Leur centre (`getCX` / `getCY`) doit être à moins de 0x80 de celui du joueur (le plus grand de |dx| et |dy|, `distance_max_xy`), leur z entre celui du joueur − 0x18 et + 0x50 (bornes exclues). Tri à bulles par distance, le plus proche d'abord.
- **Affichage** : l'objet choisi est marqué par le sprite 0x5A3 (image 0, quatre flèches vers le centre), posé à son centre par le gestionnaire de sprites d'écran (`SpriteMgr_add`, `11A8:0115` : 64 sprites dessinés par-dessus la scène). À chaque passage de la vue, le sprite suit l'objet. Une liste vide : son 0xB0 et fin immédiate.
- **Touches**, toutes prises par le mode (`13E8:1214`) : `.` `>` `s` `S` objet suivant, `,` `<` précédent (en bouclant) ; Entrée, Espace, Inser : un objet sélectionnable est utilisé (`Item_I_use`), un autre est ramassé (`MainActor_I_addItemCru`, montré) ; U : l'objet est utilisé si sa forme n'est pas un objet d'inventaire (`InvItem_index`, `1120:0424`) ; Échap et les flèches terminent le mode. Ces touches mettent fin au mode, sauf le parcours.
- **G** ramasse tout : à chaque passage de la vue, l'objet choisi est ramassé et le suivant choisi ; le mode se termine après le dernier. Pendant ce temps, les touches sont ignorées, sauf si le joueur ne peut pas agir.
- **Fin forcée** (`World_cancel_selection`, `13E8:007D`) : quand le PNJ contrôlé commence une animation (`Actor::I_doAnim` : marcher, tourner, tirer…) et sur Z. Rien n'est ramassé.
- Vérifié (`game_tests`) : sur `SAVEGAME.1`, S choisit l'interrupteur mural (forme 0x81) voisin et la capture montre le marqueur dessus ; sur `SAVEGAME.13`, avec trois objets posés à 0x20, 0x40 et 0x90 du joueur, la liste exclut le dernier, `.` et `,` bouclent, Entrée ramasse, une flèche termine ; G ramasse ensuite les deux médikits restants (à 0x10 et 0x20).

**R** bascule la désignation de cible (`DS:2889`, à 1 dans le segment de données : active au lancement), sans aucune condition, et l'annonce dans la boîte de message (voir plus bas) : « DESIGNATION DE CIBLE ACTIVE. » ou « DESIGNATION DE CIBLE INACTIVE. » (`DS:604C` / `6069`). L'indicateur appartient à la session et non à la partie : un chargement le garde. Il commande la marque posée sur la cible en mode de visée (`1138:0459`, voir `docs/combat.md`, « Mode de visée et désignation de cible »).

**Pavé numérique** : le Controller (`1138:220A`, voir `docs/combat.md`) transmet `*`, `−`, `+` et Suppr, à leur premier appui seulement.

**Non reproduit** :
- les cases de la barre d'état marquées pour être redessinées (`StatusBar_mark_dirty`) : le portage redessine toute la barre ;
- les triches et commandes de débogage de la table, suivies dans la roadmap (jalon 6.2) ;
- G : l'original tourne dans une boucle modale (`NEWGUMP_1368_0000` et la méthode `+4` de la vue) jusqu'à la fin de la liste, et appelle avant chaque ramassage la méthode 0 de la caméra (`DS:2C62`, sens inconnu) ; le portage ramasse un objet par image sans bloquer ;
- l'ordre entre le Controller et la vue du jeu pour une même touche n'est pas établi : le portage donne la touche au Controller d'abord, si bien qu'Espace tire (et met fin à la sélection) au lieu de ramasser ;
- en levant la stase, l'original sort le joueur du combat s'il y est (`Actor_clr_in_combat_flagged`, `10F0:32A8`, avec un indicateur `DS:135E` non décodé).

## Boîte de message (`MessageGump_ctor`, `1358:0046`)

Le moteur annonce un état par une boîte modale : un texte au-dessus d'un bouton « Ok » (`DS:4F5A`), centrée à l'écran, puis `Gump_run_modal` (`1328:1588`) attend sa fermeture. R l'utilise, comme les touches de triche (« Immortality enabled. »…) et quelques erreurs. Code : `src/engine/ui/message_gump.*`.

**Classes** (tables virtuelles lues dans les relocations du segment de données) : la boîte (vtable `DS:4F5D`) dérive de la fenêtre (`WindowGump_ctor`, `1300:024B`, vtable `DS:474C`) ; le bouton est un bouton de texte (`TextButton_ctor`, `1388:0000`, vtable `DS:54B0`) dérivé du bouton de base (`1310:01D0`).

**Police et couleurs** : police 0 (la police par défaut `DS:4CE6`, posée au démarrage par `TextStream_set_default_font`). La couleur c de la boîte est l'octet 5 du jeu de couleurs `DS:4D0E` (pointé par `DS:4D4E`), soit 0x14 ; le bouton reçoit c + 2 = 0x16.

**Disposition** (rectangles inclus, relatifs au parent) :
- bouton : (0, 0, largeur de « Ok » + 8, hauteur + 4) ;
- boîte : (0, 0, droite du bouton + 9, bas du bouton + 13) ; si elle est moins large que le texte + 8, son bord droit passe à largeur du texte + 8 ; puis elle grandit de la hauteur du texte (`TextStream_measure`) ;
- le bouton est centré en largeur (moitiés `(l + 1) >> 1`), son bas 4 pixels au-dessus de celui de la boîte ;
- la boîte est centrée sur l'écran de 640 × 480 de la même façon.

**Dessin** :
- cadre de la boîte (`WindowGump_draw`, `1300:038A`, relief car `+0x46` = 0 ; `Frame_draw_raised`, `1300:004B`) : contour de couleur 0, intérieur c, ombre c − 2 le long du haut et de la gauche à l'intérieur du contour, lumière c + 2 le long du bas et de la droite ;
- texte (`MessageGump_draw`, `1358:02DF`) : ligne par ligne (les `\n` sont remplacés un à un par des fins de chaîne), chacune centrée en largeur ; la plume de la ligne i (à partir de 1) est à y = haut + h × i + 4, où h est la hauteur de cette seule ligne ;
- bouton (`TextButton_draw`, `1388:00CC`), dessiné un pixel plus large et plus haut que son rectangle : en relief comme la boîte, ou enfoncé (`Frame_draw_pressed`, `1300:01A5` : contour, intérieur, lumière c + 2 en haut et à gauche) ; son texte à (x + p + 4, y + hauteur du texte + p + 2), p valant 1 s'il est enfoncé.

**Fermeture** : toute touche (méthode `+0x18`, `1358:02C8`, qui appelle la fermeture `Gump_close`, `+0x3C`). À la souris : appuyer dans le bouton l'enfonce et capture la souris ; en sortir le relève, y revenir l'enfonce de nouveau ; relâcher dedans ferme la boîte.

**Jeu suspendu** : comme pour le menu, le portage élève le niveau du noyau avec un processus muet tant que la boîte est ouverte, et redescend dès sa fermeture (`Kernel::drop_empty_levels`), pour qu'une animation lancée juste après (au relâchement d'une flèche, par exemple) reste au niveau du jeu. La scène reste affichée dessous.

Vérifié (`game_tests` sur `SAVEGAME.1`) : R ouvre « DESIGNATION DE CIBLE INACTIVE. » en (228, 217)-(411, 262), bouton en (309, 240)-(331, 258) ; une touche la ferme ; le joueur ne bouge pas tant qu'elle est ouverte ; couleurs du cadre ; un clic relâché hors du bouton ne ferme pas, dans le bouton ferme ; l'indicateur survit à un chargement. Rendu regardé sur une capture.

Non reproduit : le curseur pendant la boîte. `Gump_run_modal` reçoit −1 (pas de changement de curseur), et en jeu le curseur est caché : le portage le laisse tel quel, l'original n'a pas été vérifié sur ce point.

## Souris en jeu (méthodes de la vue du jeu, `13E8:2A13` à `13E8:3251`)

En jeu, le curseur reste caché, mais la souris pilote le PNJ contrôlé : la vue du jeu envoie au Controller (`DS:284E`, méthode `+0x28`) des touches fabriquées, que le déplaceur et le tir traitent comme celles du clavier. Le « HACK MOVER » (`DS:5FFA`, touche H avec les triches) change ces méthodes ; il est éteint en jeu normal et n'est pas reproduit. Code : `src/engine/input/mouse_queue.*`, `src/engine/world/world_mouse.*`.

**Événements** (`MouseCtl_on_driver_event`, `1360:045D`). Le gestionnaire de la souris est aussi un processus cadencé à 60 Hz, actif à tous les niveaux :
- un appui est retenu 13 tics : s'il n'y a pas de second appui du même bouton entre-temps, il part (type 1), suivi du relâchement s'il est venu (type 3) ;
- un second appui pendant ces 13 tics part aussitôt comme double clic (type 2), et le premier clic est perdu avec son relâchement ;
- appuyer sur l'autre bouton pendant qu'un appui est retenu envoie un seul appui « deux boutons » (code 0x2A) et oublie les deux ;
- un relâchement sans appui retenu part aussitôt ;
- les boutons vont dans une file de 10 événements (les suivants sont perdus) ; le mouvement n'y entre pas : seule la dernière position est gardée, et elle passe après la file (`MouseCtl_next_event`, `1360:0E1F`).

Le code de l'événement donne sa sorte (`MouseEvent_to_gump_event`, `1328:0935`), et la sorte la méthode du gump (`Gump_handle_event`, `1368:0CB2`) : bouton gauche `+8`, droit `+0xC`, mouvement `+0x10`, deux boutons `+0x24` (rien dans la vue du jeu). À chaque passage de la répartition des entrées (`1328:0624`) sans aucun événement, le gump reçoit un événement d'attente (`+4`) à la position de la souris.

**Mouvement** (`World_on_mouse_motion`, `13E8:2A13`, absente de la table des fonctions de Ghidra) :
1. Hors sélection d'objets, la souris est replacée en (320, 200) et dx = 320 − x.
2. Si le joueur n'est pas occupé (`DS:7E10`), la cadence (`DS:5FF8`) est |dx| >> (3 − vitesse) en position de combat (`1138:0070`), sinon |dx| >> (7 − 2 × vitesse). La vitesse est celle des options (`DS:6001`, 0 à 3).
3. Une cadence non nulle envoie un appui de la flèche gauche (dx > 0) ou droite (dx < 0), étendue (E0), compteur 1, avec l'état des touches de contrôle du BIOS (`DS:322C`), en levant `DS:5FF6` le temps de la traiter :
   - en mode de visée, le réticule tourne de cadence / 2 degrés, 20 au plus (`1138:10C8`), au lieu de 5 (2 sans Maj) ;
   - une marche ou une course en cours (actions 1, 3, 0x30, 0x31) ne change de direction qu'une fois toutes les 60 / (3 × vitesse + 3) tics (`1138:0F8D`, dernier changement en `DS:2883`).
4. Enfin, si le joueur marche ou tire à la souris, la méthode d'attente est appelée.

**Bouton droit** (`World_on_right_button`, `13E8:2DAD`), rien pendant la sélection :
- appui : le joueur marche (indicateur 1 de la vue) ; en position de combat, avec un bouton tenu, il se relève d'abord s'il est à genoux (0x29) et range son arme (0xB). L'original compare ensuite la direction du PNJ avec elle-même pour le tourner : elles sont toujours égales. Puis la méthode d'attente ;
- relâchement : fin de la marche, avec un relâchement de la flèche haut (E0) ;
- double clic, si le PNJ n'est pas occupé : en position de combat, il range son arme (en se relevant d'abord) ; sinon il la sort (7).

**Bouton gauche** (`World_on_left_button`, `13E8:2B74`) :
- pendant la sélection, un appui (pas un double clic) ramasse ou utilise l'objet choisi, comme Entrée ;
- sinon, un appui ou un double clic commence le tir (indicateur 8) et appelle la méthode d'attente ; le relâchement envoie un relâchement d'Espace et y met fin. L'original note aussi l'objet sous la souris (`Item_at_screen_point`, `DS:8C3D`), qui ne sert qu'au « HACK MOVER ».

**Attente** (`World_on_idle`, `13E8:307F`) : en marche, si le PNJ n'est pas occupé (et que `Actor_turn_to` vers sa propre direction n'a rien à faire), un appui de la flèche haut (E0), avec Alt en position de combat (action 0x24) ; en tir, un appui d'Espace, compteur 1, si le PNJ contrôlé n'est pas occupé (`Controller_send_fire_key`, `1150:05E8`, qui lève aussi `DS:26F7`, jamais lu). Tenir le bouton gauche tire donc comme des appuis répétés sur Espace.

Le joueur en stase (`DS:7E10`, s'il est le PNJ contrôlé) ignore les boutons, sauf le relâchement qui termine une marche.

**Portage** : en jeu, SDL est en mode relatif ; chaque mouvement s'ajoute à la position du jeu, que la vue replace en (320, 200), ce qui équivaut au replacement du pilote. En revenant dans un menu, le pointeur du système est remis à la position du curseur du jeu. Les événements passent une fois par mise à jour du jeu, avant le noyau, et l'événement d'attente part quand il n'y en a eu aucun.

Vérifié (`game_tests` sur `SAVEGAME.1`, vitesse 2) : 2 pixels ne font rien ; 20 pixels à gauche tournent le joueur debout d'un cran (direction 2 → 0), 20 à droite le ramènent ; le bouton droit tenu le fait marcher après 13 tics, et il s'arrête au relâchement ; un double clic droit sort l'arme ; en position de combat, 40 pixels tournent le réticule de 10 degrés ; le bouton gauche tenu tire (5 coups) ; un second double clic range l'arme ; pendant la sélection, la souris n'est pas replacée et un clic gauche ramasse l'objet ; le menu reprend la souris. `input_tests` vérifie la file : délai de 13 tics, double clic, deux boutons, mouvement fusionné, 10 événements au plus.

**Non reproduit** :
- dans les menus et écrans du portage, le bouton gauche agit aussitôt, sans le délai de 13 tics, et le bouton droit est ignoré ;
- les événements en file quand un menu s'ouvre sont perdus (l'original les donne au menu) ;
- l'événement d'attente part aussi après une touche du clavier (l'original ne l'envoie que si la passe n'a eu ni touche ni événement de souris) ;
- la manette.

## Touches de fonction (`World_on_key`, `13E8:14B4`)

L'écran d'aide les présente lui-même (`HELP1.DAT`) : F1 aide, F2 options, F4 chargement rapide, F5 sauvegarde rapide, F8 écran de chargement, F9 écran de sauvegarde, Alt+X retour au DOS. Chacune ouvre un écran modal (`Gump_run_modal`), le jeu restant figé derrière. Code : `Game::function_key` (`src/game/game.cpp`), `src/engine/ui/shape_dialog.*` et `help_screen.*`.

| Touche (code) | Condition | Effet |
|---|---|---|
| Alt+X (0x22D) | aucune ; testée avant Échap et avant le mode de sélection | question « Retour au DOS ? » (forme 0x15), son 0x109 |
| F1 (0x13B), `?` | vue sans mode (`+0x5B` = 0) | écran d'aide |
| F2 (0x13C) | vue sans mode | écran des options |
| F4 (0x13E) | hors « stase Crusader », PNJ contrôlé = joueur | question « chargement rapide » (0x11), son 0x107 |
| F5 (0x13F) | le joueur peut agir, hors « stase Crusader » | question « sauvegarde rapide » (0x12), son 0x108 |
| F8 (0x142) | hors « stase Crusader » | écran de chargement (`1008:0088`, mode 1) |
| F9 (0x143) | le joueur peut agir, hors « stase Crusader » | écran de sauvegarde (mode 0) |

F7 (0x141) et les autres codes de la table sont des triches, actives seulement avec `DS:08F0` (voir la roadmap, lot 6.2).

**Mode menu** (`World_enter_menu_mode`, `13E8:0E7D`, et `World_leave_menu_mode`, `0EF9`), autour de chaque écran :
- à l'entrée, les sons s'arrêtent (`AudioProcess_I_stopAllSFX`), les sprites d'écran sont suspendus (`DS:2D8F`, `SpriteMgr_suspend` : ni réticule, ni marque de cible, ni cadre de sélection), la barre d'état disparaît, le curseur 0x24 s'affiche ;
- à la sortie, sprites et barre d'état reviennent, le curseur est dépilé et caché, les touches et clics en attente sont oubliés (`Input_I_flush`), l'indicateur « téléportation Crusader » est baissé.

**Questions** (`QuestionGump_ctor`, `13F8:0000`) : une fenêtre qui ne dessine que sa forme de `GUMPS.FLX` (image 0, opaque), placée à ((640 − l) >> 1, (479 − h) >> 1). Deux boutons de forme : « oui » (0x13) en (57, 47) relatif, identifiant 1, et « non » (0x14) en (125, 47), identifiant 0. Seules les touches o O j J y Y n N s S et Échap lui parviennent ; `QuestionGump_on_key` rend 1 pour Y, J ou O après passage en majuscule (« yes », « ja », « oui »), 0 pour les autres. Un bouton activé rend son identifiant. La question de Alt+X occupe (204, 204)-(434, 273).

**Boutons de forme** (`ShapeButton_ctor`, `1378:0000`) : image 0, ou 1 quand il est sélectionné. Un point est dedans s'il est dans le rectangle et sur un pixel opaque de l'image affichée. Souris (`1310:001D` / `0151`) : appuyer dedans capture la souris et le sélectionne ; en sortir le désélectionne, y revenir le resélectionne ; relâcher dedans l'active, et il reste sélectionné.

**Réponses** :
- Alt+X, oui : `DS:6810` + 0x40 = 1, un fondu au noir bloquant (`fade_to_black_blocking`, 0x28), puis `system_shutdown`. Le portage quitte sans fondu.
- F4, oui : les sons s'arrêtent, l'emplacement 12 (`SAVEGAME.13`) est chargé (`Game_load`), puis un avis « Jeu chargé » s'affiche (`NoticeGump_ctor`, `13F8:0383` : forme 0x4E, bouton « Ok » 0x50 en (91, 47) ; Entrée, Échap ou le bouton le ferment). Sans fichier, `Game_load` ne fait rien et l'avis s'affiche quand même. Le portage charge au début de la mise à jour suivante, comme depuis le menu, puis ouvre l'avis.
- F5, oui : sauvegarde dans l'emplacement 12 (`Game_save`), puis l'avis 0x4F. Écrire une sauvegarde attend le jalon 7 : dans le portage, oui fait comme non.
- Non : retour au jeu.

**Options (F2)** et **chargement (F8)** : les mêmes écrans que depuis le menu, palette `MISC.PAL`, curseur 0x25 (remis à zéro par les options, empilé par l'écran de chargement). La fermeture des options réécrit `CRUSADER.CFG`. Un emplacement choisi au chargement est chargé (`Game_load`). Au retour : curseur 0x24 caché, palette du jeu, sortie du mode menu. F9 (écran de sauvegarde) n'est pas reproduit.

**Aide** (`HelpScreen_ctor`, `13E8:38E5`) : une fenêtre plein écran qui ne dessine rien (`HelpScreen_draw` est vide) ; chaque page est une image BMP 8 bits de 640 × 480 copiée sur l'écran effacé, avec sa propre palette (`Bmp_palette`, `1010:01EB` : entrées B, V, R, x de la table, chacune >> 2). Page 0 : `STATIC/HELP1.DAT`, page 1 : `HELP2.DAT`, page 4 : `HELP3.DAT`. Deux boutons de forme : « suivant » (0x51) en (410, 449), identifiant 0, et « sortie » (0x52) en (516, 449), identifiant 1. Curseur 0x27.
- Afficher la page `+0x49` (`HelpScreen_show_page`, `3BA5`) : −1 devient 0 sans rien redessiner ; 2 ferme l'écran ; sinon l'écran est effacé et l'image de la page dessinée (une page 3, 5 ou plus redessine la dernière image chargée), les fondus de palette sont arrêtés (type 0x206) et la palette de l'image écrite.
- Touches (`HelpScreen_on_key`, `3D99`, toutes lui parviennent) ; la page 4 redevient d'abord 0 à chaque touche : Échap, x, X ferment ; gauche, haut : page − 1 ; droite, bas, n, N : page + 1 (donc « suivant » depuis la page 1 ferme) ; e, E commencent une séquence (`+0x47` = 1) ; `-` la poursuit, et au quatrième pas la page 4 s'affiche ; toute autre touche, et tout changement de page, l'annulent.
- Boutons (`3D53`) : « suivant » page + 1, « sortie » ferme.
- Page 4 (une photo, page cachée) : à chaque passage de la boucle modale (`HelpScreen_tick`, `3EC8`), une chance sur 8 000 (`random_range(8000)` = 1) de jouer l'un des sons 0xB8, 0x62, 0x67, 0xA, 0xB (`random_range(5)`). Le portage tire une fois par tic de 60 Hz.
- Fermeture (`3AAE`) : curseur caché, palette du jeu, sortie du mode menu.

Vérifié (`game_tests` sur `SAVEGAME.1`) : F1 ouvre l'aide avec la palette de `HELP1.DAT`, barre d'état cachée et curseur visible ; flèches, séquence e − − −, n et les deux boutons à la souris ; `?` l'ouvre aussi. Alt+X : n reste dans le jeu, o quitte. F2 et F8 s'ouvrent et se ferment par Échap. Les touches de fonction ne font rien en mode de sélection. F4, y : `SAVEGAME.13` est chargé et l'avis 0x4E s'affiche, Entrée le ferme. Rendus regardés sur des captures (aide, page cachée, question de Alt+X, options, avis).

**Non reproduit** : le fondu au noir de Alt+X ; la sauvegarde rapide et l'écran de sauvegarde (jalon 7) ; l'indicateur `DS:6810` + 0x40 (posé pendant l'aide, les options et le chargement, sens inconnu) ; le vidage du clavier et des clics après un changement de page de l'aide ; l'appel à la méthode `+0x2C` de la caméra (`DS:2C62`) au retour des écrans.

## Menu principal (`IRGUMP_13d0_0000`, segment `13D0`)

Le même menu sert au démarrage (`Game_start`) et en jeu, par Échap (`WORLD_13e8_14b4`, si `DS:5FFB` = 0 et `DS:5FFC` = 1, deux drapeaux non décodés). Alt+X ouvre à la place une demande de confirmation pour quitter (voir « Touches de fonction »).

**Ouverture** (`WORLD_13e8_0e7d`, `10c3`, puis le constructeur) : sons coupés, barre d'état cachée, écran effacé, palette `MISC.PAL`, curseur image 0x25 affiché ; le KeyDaemon passe à 10 Hz et le délai de répétition à 20 ticks ; l'état du clavier est effacé (`KERNEL_11d8_33e3`).

**Dessin** : écran effacé, formes 54 à 57 de `GUMPS.FLX` (319 × 120) en (1, 120), (320, 120), (1, 240) et (320, 240), soit une zone de 638 × 240 centrée (le code calcule `(640 − 638) / 2` et `(480 − 240) / 2`). Puis six boutons, forme 58 + i, image 1 si sélectionné, coin haut gauche en (46, 170), (46, 221), (46, 271), (447, 178), (469, 271), (517, 318) : nouvelle partie, charger, sauvegarder, options, générique, quitter. Le bouton sélectionné (`DS:5BE8`, 0 au lancement) est conservé d'un menu à l'autre.

**Touches** : haut ou `8` sélectionne le bouton précédent (pas avant 0), bas ou `2` le suivant (pas après 5), gauche ou `4` recule de 3 depuis la colonne de droite, droite ou `6` avance de 3 depuis celle de gauche ; Entrée active le bouton sélectionné, Échap équivaut au bouton 6 (retour au jeu).

**Souris** (classe de bouton, segment `1310`) : appuyer dans un bouton le sélectionne et capture la souris ; relâcher dans le bouton l'active. Survoler sans appuyer ne sélectionne rien, et un bouton sélectionné reste dessiné sélectionné même si l'on sort en gardant l'appui. Le portage ne traite que le bouton gauche : le pilote donne au droit un autre code d'événement, dont l'effet sur les boutons n'est pas vérifié.

**Activation** (`IRGUMP_13d0_082e`) : 5 (quitter) et 6 (retour) ferment le menu, qui renvoie ce numéro à son appelant. Tant qu'aucune partie ne tourne (`DS:5BEE`, menu du démarrage), 2 (sauvegarder) et 6 sont sans effet. Les autres entrées ouvrent leurs écrans : difficulté (nouvelle partie), chargement, options et générique (décrits plus bas). Un niveau choisi ferme le menu avec 0 et la partie commence (`docs/missions.md`) ; l'écran de sauvegarde n'est pas encore reproduit.

**Fermeture** (destructeur puis `WORLD_13e8_10c3` et `0ef9`) : écran noir, curseur caché, KeyDaemon et délai rendus, clavier effacé ; puis curseur remis à 0x24 et caché. Sur « quitter », le jeu s'arrête ; sinon la palette du jeu revient et la barre d'état est réaffichée, même si elle était cachée avant.

**Suspension du jeu** : la boucle modale de `DISPATCH_1328_1588` n'appelle que l'affichage et le routage des événements, mais le KeyDaemon continue de tourner. Le portage élève le niveau du noyau avec un processus muet le temps du menu, ce qui gèle les processus du jeu et laisse tourner ceux de tous niveaux. Le passage instantané au noir (`PaletteFaderProcess_I_jumpToAllBlack`) n'est pas reproduit, car il n'est visible qu'entre deux écrans.

## Écran de difficulté (`IRGUMP_13d0_25a2`, segment `13D0`)

Ouvert par « nouvelle partie ».

**Mise en place** :
- palette `STATIC/DIFF.PAL` ;
- curseur 0x26 (remplacé par 0x25 au retour dans le menu), souris placée en (320, 400).

**Dessin** (`13D0:2B16`) :
- l'écran est effacé, puis la forme 0x49 de `GUMPS.FLX` est dessinée, image 0 en (139, 76) et image 1 en (331, 76) : le titre « Niveaux de difficulté » et l'emblème ;
- quatre boutons (`13D0:2D5E`), niveau 1 à 4, forme 0x49 + niveau. L'image 0 est au coin du bouton, l'image 1 à 170 pixels à sa droite ;
- chaque bouton fait 321 pixels de large (le constructeur force x1 à 320, puis centre : x 159 à 479) et la hauteur de la forme. Les bords supérieurs sont à y 120, 179, 238 et 297 ;
- le niveau sélectionné (`DS:5BEC`, 3 au lancement, conservé d'un écran à l'autre) est entouré de la couleur 0x44.

Les quatre niveaux sont « Jeu d'enfant », « Soldat du dimanche », « Gâchette facile » et « Pas de quartiers ».

**Touches** (`13D0:29B3`) :
- haut / `8` : niveau au-dessus, pas avant 1 ;
- bas / `2` : niveau au-dessous, pas après 4 ;
- Entrée : choisit le niveau sélectionné ;
- Échap : annule.

**Souris** : classe de bouton de base, comme le menu. Appuyer dans un bouton le sélectionne et capture la souris ; relâcher dedans le choisit (`13D0:2975`).

**Choix** : il fixe la difficulté (`DS:7E19`) et `DS:2172` = (difficulté ≠ 1), un indicateur aussi recalculé au chargement d'une partie et dont le lecteur n'est pas identifié. L'écran se ferme ensuite avec le niveau, ou 0 s'il est annulé.

**Vérification** : le rendu de `crusader --menu-new` correspond aux formes de `GUMPS.FLX`. `game_tests` vérifie la sélection, la palette, l'annulation et le choix à la souris.

## Écran de chargement (segment `1008`)

Le bouton « Charger jeu » du menu ouvre `LoadSaveScreen_ctor` (`1008:0088`) en mode 1, comme F8 en jeu ; « Sauvegarder jeu » l'ouvre en mode 0 (non reproduit : il faut d'abord savoir écrire une sauvegarde, jalon 7).

**Noms des sauvegardes** : l'emplacement i est le fichier `SAVEGAME/SAVEGAME.<i + 1>` (`save_file_name`). Au lancement, `SaveNames_read` lit dans chacun des 13 fichiers possibles l'entrée **9** (32 octets, texte terminé par un zéro) dans la table `DS:7964` ; un fichier absent donne un nom vide. Vérifié : `SEB`, `SEB2` à `SEB5`, `MISSION2`, `MISSION3` pour `SAVEGAME.1` à `.7`, et un nom vide pour `SAVEGAME.13`.

**Dessin** : écran effacé ; fond formé des formes 64 + 5 × mode à 67 + 5 × mode (chargement : 69 à 72, 208 × 174 chacune) en (111, 66), (319, 66), (111, 240), (319, 240), soit une zone de 417 × 348 centrée. Onze emplacements en (183, 109 + 27 i), forme 68 (314 × 17). L'emplacement sélectionné (`DS:0098`, 0 au lancement, conservé d'une ouverture à l'autre) reçoit un contour de couleur 0xFD. Son nom est écrit en police 14 en (+4, +12) ; l'original passe le nom à `printf` comme chaîne de format, le portage l'écrit tel quel. Palette `MISC.PAL`.

**Touches** : haut ou `8` (pas avant 0), bas ou `2` (pas après 10), Entrée choisit l'emplacement sélectionné, Échap ferme avec le résultat 0. **Souris** : appuyer sur un emplacement le sélectionne et le choisit aussitôt.

**Choix** (`SaveSlot_choose`) : en chargement, un emplacement au nom vide est ignoré. Sinon l'emplacement passe dans `DS:0C94` et l'écran se ferme avec le résultat 1. Le menu se ferme alors avec le résultat 1, et `World_game_menu` charge la partie (`Game_load`, `1058:02f1`) avant de revenir au jeu. Avec le résultat 0, le menu revient.

Dans le portage, le chargement a lieu au début de la mise à jour suivante, hors de la tranche du noyau : une répétition de touche, envoyée par le KeyDaemon pendant cette tranche, peut choisir l'emplacement.

**Saisie d'un nom** (mode sauvegarde, non reproduite) : 25 caractères au plus, Retour arrière, Entrée, Échap ; les lettres passent par `azerty_remap`, qui transforme la disposition QWERTY en AZERTY (version française).

## Écran des options (`IRGUMP_13d0_0ece`, segment `13D0`)

Le bouton « Options » du menu ouvre cet écran ; à sa fermeture, le menu revient (écran effacé, palette et curseur rétablis).

**Dessin** (`13d0:17c5`) : écran effacé ; fond formé des formes 44 à 47 en (1, 120), (320, 120), (1, 240), (320, 240), comme le menu ; libellés « volume son » (forme 41) en (468, 146) et « volume musique » (42) en (459, 196). Palette `MISC.PAL`. Puis les éléments, chacun avec son numéro (octet +10 du gump) et son coin haut gauche :

| Ligne | Premier élément | Second élément |
|---|---|---|
| 0 animations | 0x50 marche, forme 31, (35, 163) | 0x51 arrêt, 32, (165, 163) |
| 1 une image sur deux | 0x52 marche, 33, (35, 203) | 0x53 arrêt, 34, (165, 203) |
| 2 écran vidéo | 0x56 grand, 35, (35, 244) | 0x57 petit, 36, (137, 244) |
| 3 limite des explosions | 0x58 marche, 37, (35, 283) | 0x59 arrêt, 38, (165, 283) |
| 4 filtrage numérique | 0x54 marche, 39, (35, 323) | 0x55 arrêt, 40, (165, 323) |
| 5 volume des sons | 0x5C −, forme 49, (435, 146) | 0x5D +, 50, (597, 146) |
| 6 volume de la musique | 0x5E −, 49, (435, 197) | 0x5F +, 50, (597, 197) |
| 7 réponse de la souris | 0x60 −, 49, (435, 249) | 0x61 +, 50, (597, 249) |
| 8 | 0x62 quitter, forme 43, (517, 318) | — |

Les lignes 0 à 4 sont des choix à deux éléments (`13d0:0ce7`) : l'élément choisi a l'image 0, l'autre l'image 1. Les boutons − et + se répètent (classe `1310:032b`) et ont l'image 1 quand on les tient ; « quitter » (`1378:0000`) a l'image 1 quand il est sélectionné. La forme 48 marque chaque réglage en x = 456 + 6 n, y = 179 (sons), 230 (musique), 282 (souris), où n est le cran du volume, ou `(vitesse << 16) / 0x2164` pour la souris, moins 1 pour la vitesse 3 (0, 7, 15, 22). La ligne active (`DS:5BEA`, 0 au lancement, conservée d'une ouverture à l'autre) reçoit un contour de couleur 0x0C (`13d0:1e62`) ; la ligne 8 n'en a pas, le bouton « quitter » est sélectionné à la place.

**Volumes** : 23 crans pour les sons (`DS:5BB6`, de 0 à 0x40) et pour la musique (`DS:5BCD`, de 0 à 0x30). L'écran part du premier cran qui n'est pas inférieur au volume en cours. Au-delà du dernier cran, l'original prend le volume lui-même comme numéro de cran ; le portage prend le dernier.

**Touches** (`13d0:20b7`) : haut ou `8` et bas ou `2` changent de ligne (de 0 à 8) ; gauche ou `4` agit comme le premier élément de la ligne, droite ou `6` comme le second ; Entrée ferme l'écran sur la ligne 8, Échap le ferme toujours.

**Souris** : appuyer sur un choix qui ne l'est pas encore le choisit (`13d0:0dc0`). Un bouton − ou + agit dès l'appui, puis toutes les 11 ticks tant qu'on le tient dans le bouton (`1310:04fd` : l'échéance est « maintenant + 10 » et doit être dépassée) ; sorti du bouton, il ne se répète plus. « Quitter » agit au relâchement dans le bouton, comme ceux du menu, mais revient à l'image 0 quand on en sort. Un élément qui agit fait passer la ligne active sur sa ligne (`13d0:1d81`).

**Réglages** (`Options_key`, `13d0:236e`) :

| Réglage | Variable | Clé de `CRUSADER.CFG` |
|---|---|---|
| animations | `DS:0636` : 0 marche, 1 arrêt | `animation` |
| une image sur deux | `DS:2887` : 3 ou 0 | `frameskip` |
| écran vidéo | `DS:68CE` : mode 0x13 / 8 (grand) ou 0x101 / 0 | `bigvideo=true` (grand seulement) |
| limite des explosions | `DS:2888` | `limitblasts` |
| filtrage numérique | `_ASS_SetOversampling` / `_ASS_ClrOversampling` | `filter` |
| volume des sons | `_ASS_SetSoundVolume`, au changement de cran | `soundvolume` |
| volume de la musique | `_ASS_SetMusicVolume`, au changement de cran | `musicvolume` |
| réponse de la souris | `DS:6001`, de 0 à 3 | `mousespeed` |

Les boutons de volume et de souris jouent le son 0x98, même en bout de course. Valeurs par défaut (segment de données) : animations, filtre, son 0x40, musique 0x20, souris 2 ; une image sur deux, limite des explosions et grand écran vidéo désactivés.

**Configuration** : `CRUSADER.CFG` est lu au lancement (`1020:0000`) et réécrit à la fermeture de l'écran (`config_save`, `1020:0c7f`, appelé par le destructeur `13d0:1759`). L'original réécrit tout le fichier depuis ses variables, avec des fins de ligne `

` ; le portage remplace les lignes des clés ci-dessus, garde les autres lignes et leur ordre, et termine les lignes par `

`. Il n'écrit le fichier qu'en jeu interactif (`Game::set_config_file`), pas pendant les tests ni les rendus sans fenêtre.

**Utilisés par le portage** : les volumes et le filtre (le mixeur). Les autres réglages sont lus, modifiés et sauvegardés, mais le moteur du portage ne les applique pas encore : animations (`DS:0636`), une image sur deux (`DS:2887`, qui allonge les pauses des animations et réduit ce qui est animé hors de l'écran), limite des explosions, taille des cinématiques, vitesse de la souris.

Vérifié (`game_tests` sur `SAVEGAME.1`, `core_tests`) : l'écran s'ouvre depuis le menu avec les réglages du `CRUSADER.CFG` des données (aucune clé de volume, donc 0x40 et 0x20 ; `bigvideo=true`), la ligne 0 encadrée ; les touches changent de ligne et de cran (son 0x3D puis 0x40, sans dépasser le dernier cran) ; « musique − » tenu une seconde descend de 6 crans (l'appui puis 5 répétitions), plus rien une fois sorti du bouton ; un clic choisit « filtrage : arrêt » ; Entrée sur « quitter » ferme l'écran et écrit le fichier, qui garde ses autres lignes ; la ligne active est retrouvée à la réouverture. Les touches `-` et `=` en jeu changent le volume de la musique. Rendu comparé aux images de `GUMPS.FLX` (`crusader --menu-options`).

## Fondus de palette (`FADE.C`, segment `1440`)

Le portage garde la palette « matérielle » (le contenu du DAC VGA, ce que l'écran affiche) dans `GameState::screen_palette`, et la palette du jeu (`GAMEPAL.PAL`, `DS:058C`) dans `GameState::game_palette`. Les composantes sont gérées en 6 bits, comme dans le DAC ; la conversion vers 8 bits (`c << 2 | c >> 4`) est exactement réversible.

**Processus de fondu** (`FADE_1440_0000`, « FadeProcess », type 0x206) : arguments palette cible, fréquence, drapeau, compteur et pas. Avec une fréquence nulle, la cible est écrite aussitôt. Sinon, un processus cadencé à cette fréquence part de la palette affichée (`palette_read_hw`). À chaque tick (`FADE_1440_03a0`), il rapproche chacune des 768 composantes de la cible : du pas si elle en est plus loin, sinon il la pose sur la cible. Puis il décrémente le compteur. Le fondu s'arrête quand toutes les composantes étaient déjà égales à la cible avant le tick, ou quand le compteur atteint 0, et ce dernier tick n'écrit rien ; sinon la nouvelle palette est écrite. Un compteur de 0 ne limite donc pas la durée. Le pas vaut toujours 1 : un fondu complet à 40 Hz dure au plus 64 ticks, soit 1,6 s.

**Intrinsics** (arguments dans l'ordre de l'appel) :

| N° | Nom | Cible | Fréquence, compteur |
|---|---|---|---|
| `0x078` | `fadeToBlack()` | noir | 40 Hz, 0 |
| `0x09E` | `fadeToBlack(fréq.)` | noir | arg., 0 |
| `0x09F` | `fadeToBlack(fréq., compteur)` | noir | arg., arg. |
| `0x07B` | `fadeFromBlack()` | palette du jeu | 40 Hz, 0 |
| `0x09C` | `fadeToGamePal(fréq.)` | palette du jeu | arg., 0 |
| `0x09D` | `fadeToGamePal(fréq., compteur)` | palette du jeu | arg., arg. |
| `0x0A0` | `fadeToGivenColor(r, v, b, fréq., compteur)` | couleur unie (0 à 63, sinon arrêt fatal) | arg., arg. |
| `0x090` | `jumpToAllBlack` | noir, immédiat | — |
| `0x0DA` | `jumpToAllWhite` | (63, 63, 63), immédiat | — |
| `0x09B` | `jumpToNormalPalette` | palette du jeu, immédiat | — |
| `0x098` | `jumpToGreyScale` | luminance de la palette du jeu, immédiat | — |

Les fondus renvoient le pid du processus. `0x09C` (et `0x07B`, qui l'appelle) crée en plus un processus qui attend la fin du fondu (table virtuelle `68A7`, `FadeWaitProcess`) et renvoie celui-ci : quand il se réveille, il relance le cycle des couleurs (`1440:0F2F`, voir plus bas).

Les fondus agissent sur le cycle des couleurs (`DS:6810 + 0x40`) :
- un fondu qui a une fréquence note si le cycle était en pause et le met en pause tant qu'il tourne ;
- à sa fin, ou s'il est arrêté (`FADE_1440_0278`), il le relance s'il tournait avant, ou si son drapeau `+0x41` est posé ;
- ce drapeau est posé par `fadeToGamePal(fréq., compteur)` (`0x09D`), `fadeToBlack(fréq., compteur)` (`0x09F`) et `fadeToGivenColor` (`0x0A0`) ;
- les autres fondus vers le noir et le fondu en gris des vidéos mettent le cycle en pause avant de commencer : il reste en pause après eux ;
- les sauts vers le noir, le blanc et le gris le mettent en pause ; `jumpToNormalPalette` le relance.

L'ouverture du menu arrête les fondus en cours (`Kernel_fail_processes`, type 0x206) avant d'écrire `MISC.PAL` ; le retour au jeu réécrit la palette du jeu.

Non reproduits : `FADE_1440_0d10` et `0dfb`, des fondus bloquants (attente active jusqu'à la fin) utilisés par la sortie du jeu (Alt+X) et les transitions.

Vérifié (`game_tests`) :
- vers le noir à 60 Hz, chaque composante a perdu exactement 10 après 10 images, et le processus se termine sur une palette entièrement noire, le cycle restant en pause ;
- avec un compteur de 5, un fondu vers le blanc s'arrête à (4, 4, 4), puis son drapeau `+0x41` relance le cycle ;
- une fréquence nulle écrit la cible aussitôt ;
- le processus d'attente de `fadeToGamePal` relance le cycle à la fin du fondu.

## Cycle des couleurs (`CycleProcess`, segment `1438`)

Les lumières clignotantes et les écrans animés du décor ne sont pas des animations de formes : ce sont les entrées 1 à 14 de la palette qui changent. Le processus « CycleProcess » (type 0x248) est créé une fois, au lancement du jeu (`11D8:342C`), après l'écriture de la palette du jeu. Il est cadencé à 30 Hz et tourne à tous les niveaux du noyau. Son adresse est gardée en `DS:6810`.

À chaque tick (`CycleProcess_run`, `1438:011B`), s'il n'est pas en pause (`+0x40`), il modifie la palette du jeu (`DS:058C`) sur place :
- **rotation** : les entrées 1 à 7 prennent chacune la couleur de la suivante, la 7 celle de la 1 ;
- **sept couleurs** gardées d'un tick à l'autre (`DS:6845`) vont dans les entrées 8 à 14 (`DS:6814`). Les composantes marquées (`DS:6830`) montent de 2 et repassent à 0 au-delà de 63 (`1438:0395`). En mode 1 (`DS:6822`, entrée 14 seulement), quand une composante repasse à 0, les trois composantes reçoivent en plus `random_range(10)`, masqué à 63 ;
- **DAC** : les entrées 1 à 14 de la palette du jeu sont écrites dans le DAC.

| Entrée | 8 | 9 | 10 | 11 | 12 | 13 | 14 |
|---|---|---|---|---|---|---|---|
| Composantes qui montent | R | B | R | B | R, V | R, V, B | V |
| Valeur de départ | 0 | 0 | R = 31 | B = 31 | 0 | 0 | 0 |

Les valeurs de départ viennent d'un initialiseur statique (`1438:0480`). `1438:03FB`, la variante qui descend, n'est pas utilisée.

**Pause** (`DS:6810 + 0x40`, `GameState::cycle_paused`) :

| Posée par | Levée par |
|---|---|
| les fondus pendant qu'ils tournent, les sauts vers le noir, le blanc et le gris | `jumpToNormalPalette`, la fin de certains fondus (voir plus haut), le processus d'attente de `fadeToGamePal` |
| une nouvelle partie (`Game_start`, `1028:0000`), un changement de carte (`1028:0477`) | la fin du chargement d'une partie |
| l'ouverture du menu (`13E8:10C3`) | sa fermeture (`MainMenu_dtor`, `13D0:03F5`) |
| le début d'une vidéo (`1468:3ABC`) | sa fin (`1468:3C23`), sauf si `DS:5FFE` est posé |

Les intrinsics `0x092` et `0x094` (`13E8:0033` et `0039`, sans nom dans ScummVM) posent et effacent `DS:5FFE`. `MISS1EGG` encadre ainsi le briefing : saut au noir, `0x092`, vidéo, `0x094`, création des objets de départ, puis `fadeToGamePal`, dont le processus d'attente relance le cycle.

Non reproduits :
- le gel de débogage (`11D8:3412` : Arrêt défil enfoncé quand la variable `CRUDEBUG` est définie) ;
- la mémoire du cycle d'une partie à l'autre : l'original crée le processus une seule fois et modifie la palette du jeu sur place ; le portage le recrée, avec ses couleurs de départ et la palette du fichier, à chaque partie commencée ou chargée.

Vérifié (`game_tests`) :
- après 15 ticks, les entrées 1 à 7 ont tourné d'un cran ;
- l'entrée 10 est passée de 31 à 61, en rouge seulement ;
- les entrées 15 à 255 n'ont pas changé ;
- le DAC suit la palette du jeu ;
- une vidéo met le cycle en pause, et `0x092` le laisse en pause à la fin ;
- après le briefing de la mission 1, le cycle reprend.

Sur `SAVEGAME.1`, environ 90 pixels de l'écran de départ utilisent ces entrées.

## Générique (`WORLD_13f0_03f4`, segment `13F0`)

Le bouton 4 du menu lance le générique : curseur caché, pilote de souris arrêté, puis retour au menu (palette `MISC.PAL`, curseur réaffiché, clavier effacé).

**Données** : `STATIC/CRED.DAT` est un BMP 8 bits de 640 × 480 (en-têtes de 14 et 40 octets, table de 1 024 octets, lignes stockées de bas en haut ; segment `1010`), dessiné tel quel ; `STATIC/CRED.PAL` sa palette ; `STATIC/CREDITS.DAT` le script (3 601 octets) ; polices 16 (titres) et 17 (noms).

**Boucle** : un passage par retour de balayage vertical (60 Hz en 640 × 480) : étape de texte, attente du balayage, étape de palette. Elle s'arrête à la fin du script ou à l'appui d'une touche (`DS:322A`, posé par l'interruption clavier). Le portage fait un passage par tick de 60 Hz du noyau.

**Étape de texte** (`041d`), seulement quand aucun effet de palette n'est en cours : si la page doit être effacée, le fond est remis ; puis, à chaque changement de l'horloge à 60 Hz, un compteur d'attente (`DS:62B0`) décroît. À 0, il repart à 64 et le script est lu jusqu'à `*` ou `&` :

| Code | Effet |
|---|---|
| `!` + mot | y de la ligne suivante |
| `@` + texte | titre en police 16, centré (`(640 − largeur) >> 1`) en y ; y += hauteur + 4 |
| `$` + texte | nom en police 17, idem |
| `^` | fondu d'entrée des 128 premières couleurs |
| `#` | fin |
| `*` | arrêt de la lecture : chaque `*` attend 64 ticks |
| `&` | arrêt ; y = 20 ; fondu de sortie des 128 premières couleurs ; la page sera effacée une fois le fondu fini |

Après `*` ou `&`, si la page vient d'être effacée et qu'aucun effet ne tourne, les couleurs reviennent en fondu. Les autres octets sont sautés. Les largeurs et hauteurs viennent de `TextStream_measure` (méthodes `+0x0C` et `+0x08` de la police).

**Effets de palette** (`0776`, `07de`, `0857`) : les couleurs 0 à 127 de `CRED.PAL` sont écrites dans le DAC à max(c − niveau, 0), une fois par balayage ; sortie : niveaux 0, 4, …, 60, 63 ; entrée : 63, 59, …, 3, 0. Le reste du DAC n'est pas touché.

Non reproduits : la musique du générique (`STDINT_1200_0262(0x11)`) et la vidéo jouée quand le générique va jusqu'au bout (`DS:08F0`).

Vérifié (`game_tests`) : lancé depuis le menu, le générique écrit la première page sur le fond, atteint la fin du script en 11 579 images (3 min 13 s) et rend la main au menu ; une touche l'interrompt.
