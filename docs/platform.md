# Couche plateforme : du matériel DOS vers SDL

Ce document recense toutes les dépendances matérielles de `CRUSADER.EXE` (version française), identifiées pendant l'analyse des modules « matériels ». Pour chacune, il indique où elle se trouve dans l'exécutable d'origine et ce qui la remplace dans le portage. Les noms de fonctions sont ceux appliqués dans le projet Ghidra (voir `analysis/names_manual.tsv`).

Le jeu est écrit en **Borland C++** : constructeurs qui allouent `this` si nul, destructeurs à drapeau `&1`, vtables. Il tourne sous le DOS-extender **Phar Lap 286**, avec des instructions 386. Les modules d'origine sont identifiés par leur nom de fichier (`GRAPH\SUPERVGA.C`…).

## Vue d'ensemble

| Domaine | Modules d'origine | Accès matériel | Remplacement |
|---|---|---|---|
| Vidéo | `SUPERVGA`, `VPORT` | VESA `int 10h` 4F00/4F01/4F02, VRAM par banques, blitters asm copiés en RAM | `SDL_Window` + framebuffer 8 bits en mémoire, présenté via `SDL_Texture` |
| Palette | `BASEPAL`, `PALETTE`, `GAMEPAL`, `FADE` | DAC VGA, ports `3C7h`/`3C8h`/`3C9h` (composantes 6 bits) | table RGB 256 entrées appliquée à la conversion 8 → 32 bits |
| Clavier | `KEYBOARD`, `KDAEMON` | ISR clavier, scancodes PC, état Shift/Ctrl/Alt façon BIOS | événements `SDL_KEYDOWN`/`SDL_KEYUP` convertis en scancodes PC |
| Souris | `KDAEMON`, `MOUSECTL`, `U9MOUSE` | pilote `int 33h` avec callback, curseur dessiné par le jeu | `SDL_MOUSEMOTION`/`BUTTON` ; on garde le curseur dessiné par le jeu |
| Joystick | `SYSTEM` | port `201h` mesuré au PIT | `SDL_GameController` / `SDL_Joystick` |
| Temps | `SYSTIMER`, `SYSTEM`, `STDINT`, `KEYBOARD` (BiosTimer) | PIT (`40h`/`43h`), IRQ via PIC 8259 (`20h`/`A0h`) | `SDL_GetTicks64` ; le processus SystemTimer avance à 60 Hz |
| Son | `DSFXMAN`, `SNDMGR`, `SNDSRC`, `SNDCACHE`, `AMFLOAD`, `ASYLUM.DLL` | Asylum Sound System : DMA, IRQ, mixage dans `_ASS_UpdateDAC` | périphérique SDL (rappel) + mixeur logiciel (12 voix de sons, 8 de musique) et lecteur AMF réécrits d'après `ASYLUM.DLL` (`src/platform/sdl_audio.*`, `src/engine/audio/`) |
| Fichiers | `FILE`, `FLEX`, `FASTFLEX`, `TAGFLEX`, `FILESPEC`, `FILEPARS` | DOS `int 21h`, verrous de fichier | `std::filesystem` + `std::ifstream`, lecteur Flex (format dans `tools/flx.py`) |
| Mémoire | `CPROTMEM`, `DESC`, `DESCRIPT`, `HUGE`, `SPANKER` | sélecteurs et descripteurs en mode protégé, pointeurs `huge`, vecteurs d'interruption | **supprimé** : mémoire plate 32/64 bits, `new`/`delete` |
| Système | `SYSTEM` | `int 10h` (mode texte), sortie DOS | `SDL_ShowSimpleMessageBox` pour les erreurs fatales, `std::mt19937` ou le LCG d'origine pour l'aléatoire |

## Vidéo

- L'affichage est en **640×480, 256 couleurs**, via VESA (`svga_mode_init`, `svga_mode_set`). La granularité de fenêtre détermine le pas de changement de banque.
- Une **Surface** est une table de pointeurs de lignes plus un rectangle de clipping. Toutes les primitives passent par elle : `surface_put_pixel`, `surface_fill_rect`, `surface_hline`, `surface_vline`.
- Un **VPort** est une Surface à l'écran (drapeau 4) ou en mémoire, avec un tampon possédé ou externe (drapeau 8).
  - Les blits rapides sont des routines asm (`asm_blitters_install`), recopiées en RAM au démarrage dans un segment de code créé à la volée, puis appelées via les thunks `asm_blit_*`.
  - Dans le portage, chaque thunk devient une fonction C++ sur tampon mémoire. Il n'y a plus de distinction écran/mémoire : l'écran est lui aussi un tampon 8 bits, présenté à chaque image.
- La couleur `0xFF` est transparente (`vport_blit_transparent`).

## Palette

- Les palettes stockent des composantes VGA **6 bits** (0–63). La conversion vers 8 bits est `c << 2 | c >> 4`.
- `palette_write_hw` est le seul point d'écriture vers le matériel. Il devient « mettre à jour la table RGB du présentateur ».
- `FADE.C` (FadeProcess) produit les fondus : vers le noir, depuis le noir, vers une couleur. Ce sont des interpolations de palette, sans accès matériel direct.

## Entrées

- `KeyEvent` (12 octets) : caractère ASCII, scancode PC, drapeaux Shift au format BIOS. Le reste du jeu raisonne en **scancodes PC** : la couche SDL doit donc traduire `SDL_Scancode` vers le scancode PC.
- La souris : le pilote DOS appelle `MouseCtl_on_driver_event`, qui dessine le curseur et met en file les clics. Dans le portage, la boucle d'événements SDL appelle la même logique.
- `U9MOUSE` (curseur du jeu) découpe l'écran en 8 directions autour du personnage, avec deux zones (marcher ou courir).

## Temps

- Le moteur est un noyau de **processus coopératifs** (`KERNEL`, `PROCESS`). `SystemTimer` avance à 60 Hz, et les délais et timeouts (`DelayProcess`, `TimeoutProcess`) s'expriment en ticks.
- Le PIT sert aussi à mesurer le joystick (`joystick_read_axes`). Cette mesure disparaît avec SDL.

## Son

Détails dans `audio.md`. En bref :

- `SNDMGR` : **12 voix**. Les voix 0 à 7 servent aux sons ponctuels, les voix 8 à 11 aux sons en boucle ; si tout est occupé, une voix est reprise en tourniquet. Chaque son reçoit un *handle* 32 bits.
- `SNDSRC` : sources positionnées. Le volume dépend de la distance entre l'objet émetteur et le joueur ; celui des sons en boucle est recalculé à chaque mouvement de caméra.
- `DSFXMAN` : interface avec l'Asylum Sound System (`_ASS_Init`, `_ASS_PlaySFX`, `_ASS_UpdateDAC`…) ; `ASSProcess` est le processus de mixage.
- Les données sont dans `SOUND.FLX` (`LockTagFlex`, blocs `ASFX` de PCM 8 bits signé **non compressé** : contrairement à Ultima 8, pas de Sonarc dans cette version). La musique est au format AMF (Asylum, signature `ASYLUM`).
- Le mixeur d'`ASYLUM.DLL` (DLL NE 16 bits, pseudo-C dans `decomp/native/ASYLUM.DLL.c`) est réécrit ; le portage sort du mono 16 bits à 22 050 Hz par un périphérique SDL.

## Points durs pour les résolutions modernes

Valeurs codées en dur relevées pendant l'analyse :

| Endroit | Valeur | Effet |
|---|---|---|
| `mouse_set_range_640x480` | X 0–639, Y 0–479 | limite la souris à 640×480 |
| `svga_mode_*` | mode VESA 640×480×8 | taille du tampon écran |
| `game_mouse_new` | centre 320×200 (`0x140`, `200`) | centre du curseur directionnel, donc origine des 8 directions |
| `game_mouse_zone` | rectangles `±0x1E`×`±0x19` et `0x14..0x26C`×`0x14..0x1CC` | zones marcher/courir et bord de l'écran, en pixels 640×480 |
| `CameraProcess_I_moveTo` | `sx − 0x140`, `sy − 300` | coin haut-gauche de la vue à partir de la position de la caméra (320 = moitié de 640 ; la caméra est 60 pixels sous le centre vertical) |
| `CameraProcess_I_moveTo` | cache de 6 × 5 tuiles de 128 pixels (768 × 640) | fond de l'écran pré-rendu autour de la vue |
| `ITEMCACH_10b0_0048` | cases `s ± 5`, `d ± 3` autour de la caméra | **zone active** (voir ci-dessous) |

D'autres viendront avec l'analyse du rendu du monde (`DISPLAY`, `SPRITE`, `WORLD`).

### Zone active et résolution

La zone active (`docs/savegame.md`, « Zone active ») ne dépend pas de la taille de l'écran : 11 × 7 cases de la grille en coordonnées diagonales autour de la caméra. Une case de 512 unités fait 512 × 256 pixels à l'écran ; la zone couvre entièrement un rectangle d'environ **1 536 × 1 280 pixels** centré sur la caméra (et partiellement jusqu'à 2 048 × 1 536). En 640 × 480, les PNJ et les globs sont donc activés bien avant d'être visibles.

Conséquence pour l'étape G : une vue jusqu'à environ 1 280 × 960 reste à l'intérieur de la zone avec une marge, sans changer le jeu. En 1 920 × 1 080, les bords gauche et droit de la vue sortiraient de la zone : décors de glob absents et PNJ figés à l'écran. Il faudra alors agrandir la zone, ce qui change le gameplay (plus de PNJ actifs en même temps).

## Ordre de mise en œuvre proposé

1. Lecteur Flex, palette, décodage des formes (`SHAPES.FLX`) et présentation SDL d'un framebuffer 8 bits. Objectif : afficher un écran fixe et le comparer à une capture DOSBox.
2. Entrées SDL vers `KeyEvent` et événements souris.
3. Horloge 60 Hz et noyau de processus.
4. Mixeur 12 voix et musique AMF (faits, `docs/audio.md`).
