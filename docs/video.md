# Cinématiques

Les vidéos `FLICS/*.AVI`, leurs sous-titres `.TXT` et le lecteur qui les joue. Code du portage :
- `src/core/avi_movie.*` : fichier et décodeur ;
- `src/core/movie_subtitles.*` : sous-titres ;
- `src/engine/ui/movie_player.*` : lecteur ;
- `src/engine/usecode/movie_intrinsics.*` : fonctions de script ;
- `Game::play_movie`.

Les vidéos se lancent depuis les scripts. Dans le portage, on peut aussi en jouer une avec `crusader data --movie 1a` ou `--movie-overlay mva5b`.

## Fichiers

Les fichiers sont des AVI RIFF ordinaires, avec des codecs propres à Origin.

| Codec | Fichiers | Taille | Bandes | Son |
|---|---|---|---|---|
| `RRV2` | 162 (missions, `MVA*`, `O01`, `T02`) | 240×180, 160×118 à 122 (`MVA*`), 320×120 | 15 (13 en surimpression) | oui, sauf `MVA*` |
| `RRV1` | `B01`, `H01`, `O02`, `S01`, `WEC` | 320×120 à 160 | 5 | oui |
| `JYV1` | `16A` | 240×180 | 5 | non |

**En-tête** (`1468:2188`) :
- `avih` donne la taille ;
- pour chaque flux, `strh` donne le type, le codec et le nombre d'images ;
- `strf` de la vidéo est un `BITMAPINFOHEADER` suivi de sa table de couleurs ; `biClrImportant` (236) est le nombre de couleurs utilisées.

**Son** : flux `JYA1`, PCM 8 bits non signé, mono, 22 050 Hz, 1 470 octets par image. Les 4 premiers morceaux (« initial frames ») sont en tête de la liste `movi`.

**Morceaux de la liste `movi`**, lus à la suite :
- `00dc` : image pleine largeur ;
- `00db` : image demi-largeur, en RRV seulement (en JYV1, elle est traitée comme pleine) ;
- `01wb` : son.

### Format d'une image

L'image est découpée en bandes horizontales de `hauteur / bandes` lignes ; le reste en bas n'est jamais dessiné. Le morceau commence par un décalage u32 par bande, depuis son début. Une bande contient :
- `u32 n` : taille des codes de longueur, 0 si la bande n'a pas changé ;
- `n` octets de codes, lus bit de poids fort d'abord (`bits_msb`, `1460:0000`) ;
- puis les pixels à copier.

Les longueurs alternent : sauter (garder le pixel de l'image précédente), copier (prendre les pixels suivants), sauter… La première longueur est un saut, et les lignes se suivent. Une longueur est un code `c` de 4 bits plus des bits supplémentaires : `base[c] + les extra[c] bits suivants`, sans bits supplémentaires pour les codes 0 et 8.

| c | 0 | 1 | 2 | 4 | 5 | 6 | 8 | 9 | 10 | 12 | 13 | 14 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| base (`DS:69C2`) | 0 | 128 | 8 | 2 | 0 | 32 | 1 | 256 | 16 | 4 | 0 | 64 |
| bits (`DS:69B2`) | – | 7 | 3 | 1 | 16 | 5 | – | 8 | 4 | 2 | 24 | 6 |

Les codes 3, 7, 11 et 15 n'apparaissent pas.

En demi-largeur (`1468:04F9`) :
- les longueurs comptent des paires de pixels ;
- chaque octet est dessiné deux fois (`HUGE_12b0_1596`).

Les pixels sont des indices de palette. Dans les vidéos à palette, ils vont de 10 à 245.

## Lancement par les scripts

Les fonctions de script reçoivent l'objet, le nom, puis x et y. Les coordonnées ne servent pas : la vidéo est toujours centrée. Toutes renvoient 0.

| Intrinsic | Fonction d'origine | Fondu en gris | Palette | Bandes RRV2 | Sous-titres | Musique |
|---|---|---|---|---|---|---|
| 0x0D2, 0x00F `playMovieCutscene` | `1468:3F77` | oui | celle de la vidéo | 15 | si activés | en pause |
| 0x093, 0x0AA `playMovieCutsceneAlt` | `1468:4060` | non | celle de la vidéo | 15 | si activés | en pause |
| 0x060 `playMovieOverlay` | `1468:4169` | non | celle du jeu | 13 | non | continue |

`playMovieCutsceneAlt` lit en plus tout le fichier en mémoire d'abord (tampon de 3 Mo au lieu de 384 Ko).

**Nom du fichier** (`1468:2F7C`, en majuscules) :
- `MVA` suivi d'un non-chiffre en 5ᵉ position reçoit un `0` après `MVA` : `mva5b` → `MVA05B` ;
- un chiffre suivi d'un non-chiffre reçoit un `0` devant : `1a` → `01A` ;
- le reste est gardé tel quel.

Le chemin est `flicpath` (`CRUSADER.CFG`) + `\` + le nom, avec `.TXT` pour les sous-titres puis `.AVI`. Si la vidéo n'existe pas, rien ne se passe. C'est le cas de `test`, `hideoutx`, `m03`, `z1`… que les scripts appellent aussi.

La vidéo se joue à l'intérieur de l'appel : le jeu est figé jusqu'à la fin. Le portage rend la main : l'appel termine la passe du noyau (`Kernel::interrupt_slice`), puis `Game::update` joue la vidéo au lieu de faire tourner le noyau. Le script reprend après l'appel à la passe suivante.

## Déroulement

**Préparation** (`1468:3ABC`, `354A`) :
1. Le curseur est caché, le cycle des couleurs mis en pause (voir `docs/interface.md`).
2. La palette à l'écran est gardée, pour la remettre à la fin.
3. Pour `playMovieCutscene`, fondu vers le gris (`fade_to_greyscale_blocking`) :
   - chaque entrée va vers sa luminance YIQ, `(299 R + 587 G + 114 B) / 1000` ;
   - 40 Hz, pas de 1, jusqu'à l'atteindre ;
   - environ 1,2 s depuis la palette du jeu, aussitôt fini depuis un écran noir.
4. Autour de la vidéo, un cadre de `GUMPS.FLX` en (x + 2, y − 1) (`1468:37C4`) :
   - 0x34 si moins de 130 lignes et moins de 162 pixels de large ;
   - 0x35 si moins de 130 lignes ;
   - 0x33 si moins de 182 lignes ;
   - aucun sinon ;
   - une vidéo agrandie a un rectangle noir à la place.
5. Sauf en surimpression, l'écran (vue, barre d'état, cadre) passe en niveaux de gris (`vport_remap_luma`) : indice = luminance / 4, moins 16 au-dessus de 7.
6. Sauf en surimpression, nouvelle palette :
   - les couleurs de la vidéo à partir de (256 − n) / 2 ;
   - de part et d'autre, deux rampes de gris (2, 4… en bas ; 2 × début, + 2… en haut), que les niveaux de gris utilisent ;
   - les entrées 8 et 9 deviennent blanc et rouge.
7. Les morceaux de son initiaux sont lus.
8. Les bruitages sont arrêtés et, sauf en surimpression, la musique est mise en pause.

**Lecture** (`1468:2869`) : un tic par bande, à `bandes × 15` Hz, soit 15 images par seconde. À chaque tic :
- Échap, Espace ou Entrée enfoncée arrête la vidéo ;
- la vidéo s'arrête aussi à l'image `nombre − 1` (la dernière n'est jamais montrée) ou à la fin de la liste `movi` ;
- bande 0 : on passe à l'image suivante. Si le morceau courant n'est pas une image, le tic ne fait que compter une image ; c'est ce qui raccourcit la fin des vidéos de 3 images ;
- dernière bande : on passe au morceau suivant. Puis, jusqu'à 4 images de la fin, un morceau de son est mis en file et passé. Le son démarre (canal 0, volume 0x41) dès qu'il y a plus de morceaux que les initiaux ;
- bande 1, avec les sous-titres : voir plus bas.

**Grand format** (`bigvideo=true`, réglé dans l'écran des options) : la vidéo est doublée en largeur et en hauteur, sauf si elle fait 640 de large ou 480 de haut, et sauf en surimpression.

**Fin** (`1468:3D7A`, `3C23`) :
1. Son arrêté, musique relancée, touches oubliées (`keyboard_flush`).
2. Sauf en surimpression, écran effacé et palette d'avant remise.
3. Le cycle des couleurs reprend, sauf si l'intrinsic `0x092` a posé `DS:5FFE` (recopié au lancement dans le champ `+0xC2` du lecteur) ; `0x094` l'efface.
4. Curseur réaffiché.

## Sous-titres (segment `1470`)

Ils ne s'affichent qu'avec `subtitles=true` dans `CRUSADER.CFG` (`DS:0179`), le seul moyen de les activer. Le portage lit et réécrit cette clé.

Le fichier est fait de lignes `@frame n`, chacune suivie de la ligne de texte à montrer à partir de l'image n. `Subtitles_parse` en fait une entrée par ligne `@frame` :
- **délai** : le numéro du `@frame` suivant moins celui-ci. Pour la dernière entrée, 0 − n, un délai qui n'expire jamais ;
- **texte** : la ligne suivante, sauf si elle commence par `@`, ou ni par une lettre ASCII ni par `.`. Une réplique commençant par un chiffre ou une lettre accentuée est donc ignorée par l'original ;
- **largeur** : la somme des avances des caractères sauf le dernier (police 4).

Si le premier délai vaut 0, les sous-titres sont désactivés.

À la bande 1 de chaque image, quand le délai de l'entrée affichée est écoulé (il part de 0), on prend l'entrée suivante. Elle est écrite dans une bande noire de 601 × 16 :
- police 4, centrée, 2 lignes plus bas ;
- placée en (20, y), avec y à 8 lignes sous la vidéo, 470 au plus.

## Classes des événements des scripts

Cette lecture a fait trouver la règle qui choisit la classe usecode recevant les événements d'un objet (`Usecode_class_of`, `1420:0DC5`). Le portage l'ignorait ; c'est elle qui lance le briefing de la mission 1. Voir `docs/usecode.md`.

## Vérification

Par `game_tests` (`test_movies`, `test_new_game`) sur les vraies données :
- les 940 images de `01A.AVI` se décodent, avec des pixels de 10 à 245 ;
- `WEC.AVI` (RRV1, demi-largeur) se décode ;
- `01A.TXT` donne 15 entrées : la première sans texte, la deuxième « Je suis le général Maxis. » 65 images plus tard ;
- depuis `SAVEGAME.1` : le jeu reste figé, le fondu en gris dure 30 à 150 images, la palette est conforme, environ 150 images de vidéo s'affichent en 10 s, le sous-titre apparaît sous la vidéo, Échap rend la palette du jeu ;
- en surimpression, la palette du jeu est gardée ;
- une nouvelle partie joue le briefing `01A`, lancé par `MISS1EGG`.

Les rendus (`--png`) montrent :
- le général Maxis net, en petit format (cadre 0x33) et en grand ;
- le fond en niveaux de gris ;
- le sous-titre accentué ;
- la surimpression `MVA05B` dans son cadre d'écran (0x34).

Le son mixé correspond à la piste (corrélation 0,99).

Reste à confirmer sous DOSBox : une incrustation rouge et verte vers l'image 300 de `01A`. Elle apparaît aussi avec un second décodeur indépendant, écrit en Python, mais qui suit la même lecture du format.

## Non reproduit

- L'affichage de débogage du tampon de lecture (`DS:0905`).
- La lecture du disque par blocs de 4 Ko : le portage a tout le fichier en mémoire, la lecture n'attend donc jamais le disque.
- Les processus cadencés : ils continuent pendant une vidéo dans l'original (interruption de l'horloge), le portage les fige avec le reste.
- Le son : le portage joue un tampon fait de tous les morceaux de son, au lieu d'un anneau de 8 morceaux rempli au fil de la vidéo. C'est identique tant que la cadence tient.
- La reprise du script : il reprend à la passe suivante du noyau, et non au milieu de la passe interrompue.
- Le cas d'un morceau vide dans `movi`, qui ferait redessiner la dernière bande. Aucune vidéo n'en a.
