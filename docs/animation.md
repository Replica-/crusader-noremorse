# Animations des personnages et collisions

## `STATIC/ANIM.DAT` (`ANIMFILE.C`, `ASTREAM.C`, `ANIMCACH.C`)

Ce n'est pas un Flex. Le fichier commence par 2 048 positions sur 32 bits, une par forme (0 = forme non animée). Chacune mène à une table de 256 positions, une par action. Une action est un en-tête de 4 octets suivi de « nombre d'images × directions » images de 8 octets, rangées par direction.

**En-tête d'action**

| Octet | Contenu |
|---|---|
| 0 | nombre d'images |
| 1 | drapeaux : bit 0 = en boucle ; bit 2 = la boucle reprend à l'image 1 ; autres bits lus par le processus d'animation |
| 2 | bits 0–3 : ticks entre deux images ; bit 4 : déplacement tourné de +4 (en biais) |
| 3 | bit 6 : 16 directions (sinon 8 : la direction 0–15 est divisée par deux, `ASTREAM_1168_0106`) |

**Image** (8 octets)

| Octets | Contenu | Lu par |
|---|---|---|
| 0–1 | bits 0–11 : image de la forme (masque `0xFFF`, `ANIMPRIM_1158_1908`) ; `0x1000` : attaque spéciale de quelques PNJ (`WPNCACHE_1160_01f0` : explosion de certains robots, sons ; posé seulement dans les actions `0x1C`/`0x1D` du joueur et `0x1E`/`0x1F` de PNJ), **pas** le tir ; `0x4000` : événement d'image (`ITEM_10a8_2b01`) ; `0x8000` : image en miroir (drapeau d'objet `0x20`) | |
| 2 | variation de z (signée) | |
| 3 | bruitage (0 = aucun) | `AudioProcess_I_playSFXCru` |
| 4–5 | bits 0–8 : distance signée ; bit 9 : le pas vérifie le sol et peut monter une marche (balayage `0x11`, sinon `4`) | |
| 5–7 | **tir** : décalages de la bouche du canon (bits 6–10 du mot 5–6 et bits 3–7 de l'octet 6, signés sur 5 bits ; bits 0–3 de l'octet 7). L'arme tire sur l'image où l'un d'eux n'est pas nul (`WPNCACHE_1160_005f` → `Item_I_fireWeapon`) ; voir `docs/combat.md` | `ANIMPRIM`, `WPNCACHE_1160_005f` |

Vérifié sur tout le fichier : 41 formes animées, 357 actions, 22 056 images, toutes présentes dans leur forme.

Actions du joueur (forme 1), noms repris de ScummVM et cohérents avec les données : 0 immobile, 1 marcher (17 images, boucle, 64 unités par cycle), 2 reculer, 3 courir (12 images, 96 unités), 4 garde.

**Numéro d'image des PNJ** : la table des objets ne garde que l'octet bas ; pour les identifiants 1 à 255, l'octet haut est dans la fiche du PNJ (`+0x0F`, `Item_I_getFrame` / `Item_I_setFrame`). Le joueur de `SAVEGAME.1` est à l'image 635.

## Processus d'animation (`ANIMPRIM_1158_016f`, `ANIMPRIM_1158_1908`)

Chaque pas affiche l'image suivante, déplace l'acteur, puis attend le nombre de ticks de l'action. La direction est dans la fiche du PNJ en `+0x0C`, la dernière action lancée en `+0x0D`, l'indice d'image en `+0x10` ; cet indice passe d'une animation à la suivante.

**File d'attente** (`Actor_I_doAnim`, `Process_suspend_by_type`, `KERNEL_11d8_17db`) : une animation nouvelle attend la dernière animation en file du même acteur. `doAnim` refuse si l'acteur est rangé (z = 254), mort, bloqué (bit 0x40 de `+0x59`, que rien ne pose dans cette version), sans cette action, ou s'il a déjà plus d'animations en file que permis (10 000 par défaut ; 0 = refus si occupé) ; il peut aussi annuler les animations en cours. Intrinsic `0x039` : `doAnim(pnj, action, direction, file maximale, annuler)`, qui renvoie le pid. `0x00E` `turnToward(pnj, direction, toujours)` fait tourner le PNJ cran par cran dans sa posture (`SPRITE_1150_009C`, voir `docs/combat.md`) et renvoie le dernier pid.

**Direction** (`ANIMPRIM_1158_31da`) : `0x10` = direction actuelle de l'acteur au démarrage, `0x11` / `0x12` = un cran à droite / à gauche (deux pour une action à 8 directions, qui n'utilise que les directions paires).

**Indice de départ** (`ANIMPRIM_1158_17b2`, au démarrage effectif, après l'attente) : action en boucle → milieu du cycle si le bit 2 de `+0x59` est à 0, sinon dernière image (drapeau d'action `0x04`) ou 0 ; action simple avec drapeau `0x04` répétée → 1 ; sinon 0.

**Demi-pas** : une action en boucle avec le drapeau `0x04` (la marche, 17 images) s'arrête à l'avant-dernière image (sauf si l'animation suivante en file est la même) en mettant le bit 2 de `+0x59`, puis la suivante va de la dernière image, en repassant par 1, jusqu'au milieu, où elle s'arrête en effaçant le bit. La marche se fait donc en demi-pas (images 8 à 15, puis 16 et 1 à 7), chacun pouvant céder la place à une autre animation quand un pied est posé.

Déplacement : `x += dx[dir] × 2 × distance`, `y += dy[dir] × 2 × distance`, avec les tables du segment de données (16 directions, 0 = vers −y) :

| Table | Valeurs |
|---|---|
| `DS:132F` (dx) | 0 1 1 2 1 2 1 1 0 −1 −1 −2 −1 −2 −1 −1 |
| `DS:1340` (dy) | −1 −2 −1 −1 0 1 1 2 1 2 1 1 0 −1 −1 −2 |

Hors de l'écran, l'original saute une image sur deux en cumulant les distances (`+0x41`) ; pas encore reproduit.

## Collisions (`ITEMFIND.C`, segment `10E8`)

**Boîtes** : un objet en (x, y, z) d'empreinte (fx, fy, fz) occupe x − 16 fx + 1 … x, y − 16 fy + 1 … y, z … z + h − 1, avec h = 8 fz, ou 1 pour une forme plate non fixe. Un objet en miroir échange ses étendues x et y.

**Masques** (`FUN_1018_0000`) : `0x8000`, plus 1 si la forme est solide, `0x20` si elle blesse. Un contact bloque quand les masques ont un bit commun sous `0x100`.

**Contact objet par objet** (`10e8_032e`) : pour chaque face (+x, +y, +z, −x, −y, −z dans cet ordre), on cherche l'instant où la boîte en mouvement l'atteint, et on vérifie que les deux autres axes se recouvrent à cet instant (calcul en entiers 32 bits). Un seul contact par objet. Le même test repère le sol porteur : un objet solide dont le sommet est sous le point d'arrivée, à un écart permis près (0, ou 9 avec le drapeau `8`).

**Balayage** (`10e8_0b39`) : candidats = objets de la grille dans le rectangle parcouru, élargi de la boîte et de `0x100` ; l'acteur lui-même est exclu. Le contact bloquant le plus proche tronque le déplacement. Refus si une des boîtes de départ ou d'arrivée sort de la zone active, ou si le point d'arrivée doit reposer sur un sol (drapeau `4` absent) et n'en a pas.

**Place libre** (`10e8_00d9`, avec `1018:00FD`) : la boîte d'une forme en un point (x − l + 1 … x, y − p + 1 … y, z … z + h − 1, au plus 255) est libre quand aucun objet dont la boîte la recoupe n'a de bit commun avec le masque de la forme (bits sous `0x8000`). Rien n'est exclu, pas même l'objet qui se tient là. Les boîtes des objets trouvés ignorent leur miroir, et les candidats sont les objets de la grille jusqu'à `0x100` au-delà de la boîte. Une boîte qui passe sous 0 en x ou en y n'est jamais libre ; une boîte qui couvre z de 0 à 255 l'est toujours. Le premier objet solide trouvé est rendu (`10e8_1874`, `Map_position_free`). Servent ce test : `legalCreateAtPoint` / `legalCreateAtCoords` (formes jusqu'à 0x7FF), `canExistAt`, le départ des tirs (`fireWeapon`, `fireDistance`) et la transformation en forme 0x576 (`1130:026B`, qui retire le PNJ du monde pendant le test). Avant ce jalon, le portage faisait un balayage de longueur nulle, qui ne touche jamais rien : ces tests étaient toujours vrais.

**Marcher** (`10e8_162f`) : sur un déplacement de niveau, on accepte un sol jusqu'à 9 unités plus bas puis on tente d'y descendre ; si c'est bloqué et que le drapeau `0x10` est mis, on retente 8 puis 9 unités plus haut (marches). En quittant une marche, la descente part d'une boîte encore posée sur la marche et la touche : l'acteur reste un pas à la hauteur de la marche, puis descend au pas suivant.

Les recherches de collision rangent leurs résultats dans le tableau `7E01` : c'est le tableau 11 des sauvegardes, un simple tampon de travail.

## Chute (segment `1040`)

**Appui** (`1040:0072`) : l'empreinte est coupée en quatre par ses milieux ; chaque objet solide (masques compatibles) dont le sommet est au niveau des pieds — ou jusqu'à 9 plus bas avec tolérance — marque les quarts qu'il couvre (8 : x bas / y bas, 4 : x bas / y haut, 1 : x haut / y bas, 2 : x haut / y haut). `0x10` quand deux quarts opposés en diagonale sont couverts : l'acteur tient debout. Avec tolérance, le résultat est nul si aucun appui n'est exactement au niveau. Au sol (z = 0), tout est couvert.

**Après un pas qui vérifie le sol** (`ANIMPRIM_1158_3091`) : si l'appui au point d'arrivée n'a pas `0x10`, ou si le pas a été refusé faute de sol (sans obstacle), l'acteur trébuche (action `0x3B`, qui remplace ses animations, sauf roulades et positions à genoux) et part en chute avec le pas comme vitesse.

**Processus de gravité** (type `0x203`, `1040:0395` / `06D2` / `04BC`) : vitesse (vx, vy, vz) et gravité ; `Item_I_hurl` le crée (puis le fait attendre 20 ticks) ou ajoute la vitesse à celui qui existe ; `Item_I_fall` = `hurl(0, 0, 0, gravité 2)` pour les objets non fixes et pesants. Tous les 4 ticks : vz −= gravité, balayage vers (x + vx, y + vy, z + vz), contre un mur un PNJ perd sa vitesse horizontale (un objet la renverse), contre un mur vz est divisé par 4, à l'atterrissage la chute s'arrête (objets sans les drapeaux de rebond `0x4400`). Abandon après 200 pas. Pendant la chute, la fiche du PNJ a le bit 1 de `+0x59` et la hauteur de départ en `+0x11` ; le processus d'animation attend. À la fin d'une chute qui n'a pas échoué (`04BC`), le joueur vivant tombé de 3 crans de 8 ou plus atterrit lourdement : action 0x3B (remplaçant les animations en cours), puis 4 en combat ou 0, et le son 0x8F ; sauf si sa dernière action est 0x3A, 0x3D, 0x3E, 0x17, 0x18 ou une course (`Actor_is_running`). Un PNJ mort arrête son processus de type 0xF0 (non reproduit). Vérifié (`game_tests`) : le joueur lâché de 48 unités au-dessus du sol de `SAVEGAME.13` atterrit par l'action 0x3B avec le son 0x8F.

## Clavier et contrôle du joueur

**Interruption clavier** (`KERNEL.C`, 11D8:3574, routine en assembleur) : pour chaque touche (tables normale `33D6` et étendue `36D6`, 6 octets par touche), l'heure de l'appui et un compteur ; un événement de 12 octets par appui ou relâchement (`3254`, file de 32). Une touche déjà enfoncée est ignorée : la répétition automatique du clavier n'est pas utilisée.

**KeyDaemon** (`KDAEMON.C`, processus temporisé à 45 Hz, tous niveaux) : chaque touche tenue depuis au moins 10 ticks (`DS:2DC2` ; 20 dans certains écrans) est renvoyée à chaque tick avec un compteur croissant (événement `+10`).

**Contrôle du joueur** (`CRUSADER_1138_12cf`, appelé pour chaque événement) :

| Touche | Effet (debout, sans Ctrl ni Alt) |
|---|---|
| ↑ | marcher (action 1) ; Maj : `0x22` puis courir (3, ou `0x30` arme en main) ; Verr Maj inverse |
| ↓ | reculer (2) |
| ← → | tourner (pose immobile dans la direction voisine) ; en marchant, la marche change de direction |
| pavé numérique | directions absolues à l'écran : 8 → 14, 9 → 0, 6 → 2, 3 → 4, 2 → 6, 1 → 8, 4 → 10, 7 → 12 (relatives avec Verr Num) |
| Début, PgPréc, Fin, PgSuiv, Inser, Suppr (étendues) | `0x24`, `0x18`, 2, `0x1D`, `0x17`, `0x1C` (variantes accroupi) |
| Ctrl / Alt + touches | roulades et pas de côté (`0x17`, `0x18`, `0x1C`, `0x1D`, `0x3D`, `0x3E`, `0x0B`, `0x3A`…) |

Un appui lance l'action si le joueur est libre ; s'il est occupé par une action qui l'accepte (`CRUSADER_1138_0f8d` : marche, course…), seule la direction change. Au relâchement de la dernière touche de déplacement, une pose immobile est mise en file : 0 après la marche, `0x27` après la course, 4 après les mouvements de combat, `0x2E` accroupi. La marche continue donc grâce aux répétitions du KeyDaemon, un demi-pas à la fois.

## Pas encore reproduit

Réaction de l'original à un pas bloqué par un obstacle (glissement, événements de contact via `REFSTORE` ; le portage arrête l'animation), rebonds et roulement des objets lancés, glissade au bord d'un appui, événements d'image, saut d'images hors écran, suivi de hauteur en combat, cas particuliers du démarrage (`016F` : chute, action `0x40`…). Les modes de visée du contrôle (`DAT_2863` / `2864`) et le drapeau de rotation (`0692` / `2E55`) sont décrits dans `combat.md`, « Mode de visée ».
