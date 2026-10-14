# Son

Code d'origine lu : `DSFXMAN.C` (segment 12E0), `STDINT.C` (1200, la musique), `AMFLOAD.C` (12E8), `SNDMGR.C` (12F0), `SNDCACHE.C` (12F8), `SNDSRC.C` (1450), les intrinsics de `ITEM.C` (10A8:3934-39EB) et l'Asylum Sound System (`ASYLUM.DLL`, « Written by Jason Ely », 1994-95 : DLL NE 16 bits dont le pseudo-C est dans `decomp/native/ASYLUM.DLL.c`).

Portage : `src/core/sound_flex.*`, `src/core/amf_module.*`, `src/engine/audio/` (mixeur, lecteur de musique, voix, sources, musique du jeu, `Audio`), `src/engine/usecode/audio_intrinsics.*`, `src/platform/sdl_audio.*`.

## Données : `SOUND/SOUND.FLX`

Un Flex ouvert comme `LockTagFlex` (`TagFlex_ctor`, 1258:0000). L'entrée 0 est la table des noms : pour chaque entrée 1..n, un u32 (date et heure DOS) puis le nom terminé par un zéro (`FOOT1`, `LASER71`…). **Le son numéro n est l'entrée n.** Les noms le confirment : 0x55 `SHOTGN32` est le son de tir du fusil à pompe, 0x19 `COCK1` son armement, 0x2A `CLICK` l'arme vide, 0xD0 `INTRUDER` l'alarme.

512 entrées, 269 sons. Chaque son est un bloc `ASFX` passé tel quel à `_ASS_PlaySFX` :

| Offset | Taille | Contenu |
|---|---|---|
| 0x00 | 4 | `ASFX` |
| 0x04 | u32 | longueur (octets = échantillons) |
| 0x08 | u32 | début de boucle |
| 0x0C | u32 | longueur de boucle (boucle si > 2) |
| 0x10 | u16 | non lu |
| 0x12 | u16 | fréquence : 11025 (183 sons), 22050 (59), 11111 (27) |
| 0x14 | — | échantillons 8 bits signés, mono |

**Pas de compression** : contrairement à Ultima 8, il n'y a pas de Sonarc dans cette version. Beaucoup d'entrées portent des octets après la longueur (12 octets pour 53 d'entre elles, davantage pour d'autres) ; ils ne sont jamais joués.

Le cache (`SNDCACHE.C`) charge les sons à la demande dans une réserve de mémoire fixe et libère le moins récemment utilisé, ce qui arrête ses voix (`SoundCache_get`, 12F8:052D). Le portage garde `SOUND.FLX` en mémoire (5 Mo) : pas de cache.

## Mixeur (`ASYLUM.DLL`)

`_ASS_Init` reçoit la configuration de `CRUSADER.CFG` : fréquence de mixage `mixrate` (DS:4648, 22 050), volume des sons (DS:464B, 0x40), de la musique (DS:464D, 0x20), suréchantillonnage `filter` (DS:464F, vrai par défaut). Le portage lit ces trois dernières clés (`soundvolume`, `musicvolume`, `filter`) ; l'écran des options les change et les sauvegarde (voir `docs/interface.md`). En jeu, les touches `=` et `-` (codes 0x0D et 0x0C, `CRUSADER_1138_0896`) montent et baissent le volume de la musique d'une unité, jusqu'à 0x30. `ASSProcess` (type de processus du noyau) appelle `_ASS_UpdateDAC`, qui remplit le tampon DMA de la Sound Blaster.

Le mixeur travaille **par tampon**, d'une durée d'un tick de musique (DS:00A1 échantillons : fréquence / 50 au départ, puis selon le tempo du morceau, 441 à 450 échantillons à 22 050 Hz). Pour chaque tampon (1000:0D6C, cartes 16 bits) :

1. sauf musique arrêtée ou en pause, le lecteur de musique avance d'un tick et ses **8 canaux** sont mixés au volume de la musique (DS:0096) ;
2. les demandes de sons sont appliquées (1000:0A42) : `_ASS_PlaySFX(échantillon, canal, volume, hauteur, boucle)` (1010:0024) ne fait que poser une demande, donc un son démarre au tampon suivant, de même qu'un arrêt ou un changement de volume ;
3. les **12 canaux de sons** sont mixés au volume des sons (DS:00CC) ; un canal arrivé au bout se libère et rappelle le jeu (1018:078C → `sound_mark_stopped`, enregistré par `_ASS_RegisterCallback`).

`_ASS_IsChannelBusy` répond vrai dès la demande posée, jusqu'à la fin du son. Pour chaque échantillon de sortie et chaque canal (1000:0166, structure de 0x41 octets à DS:0928) :

1. la position avance du pas en virgule fixe 16.16 : `(fréquence << 16) / 22050 + hauteur` pour un son (hauteur limitée à 0x358 ; le jeu passe toujours 0), calculé par le lecteur pour la musique ;
2. un canal dont la boucle dépasse 2 recule de la longueur de boucle en atteignant début + longueur ; les autres s'arrêtent à leur longueur ;
3. le volume v (0..64) du canal est multiplié par le volume g (sons ou musique) : `v * g / 64` (table DS:CD00) ; la valeur 0x41 contourne g, à double échelle ;
4. l'échantillon s ajoute `s * v / 2` (table DS:3FFE construite par 1018:0360). Les valeurs négatives sont traitées en signe et grandeur autour de 0xFF : −1 donne 0, −128 donne `−127 * v / 2`.

La somme est écrite sur 16 bits. Avec le suréchantillonnage, chaque sortie est la moyenne de la somme et de la précédente (un filtre passe-bas). Un son à plein volume culmine donc à ±4 064, ce qui reste discret : c'est le niveau d'origine.

Le portage (`AsylumMixer`, `AsylumVoice`) reproduit ces calculs et ce découpage. Différences :

- le portage écrit sur un périphérique SDL en mono 16 bits ;
- la somme est bornée au lieu de déborder ;
- la fin d'un son est lue par le jeu (`busy`) au lieu d'un rappel ;
- quand l'effet F change le tempo, le tampon en cours garde l'ancienne longueur (l'original mixe peut-être déjà la nouvelle : non vérifié).

Sans périphérique (tests, `crusader --png`), `Game::update` mixe lui-même le temps écoulé pour que les sons finissent ; `--wav fichier.wav` enregistre ce mixage.

## Musique

### Fichiers `SOUND/*.AMF`

« ASYLUM Music Format V1.0 » : des modules ProTracker convertis par `MOD2AMF.EXE`, chargés en une fois par `amf_load` (12E8:00A6, qui ne vérifie que « ASYLUM » : « Invalid AMF format »).

| Offset | Taille | Contenu |
|---|---|---|
| 0x000 | 0x20 | signature, complétée par des zéros |
| 0x020 | u8 | vitesse initiale (ticks par ligne, 5 ou 6) |
| 0x021 | u8 | tempo initial (125 partout) |
| 0x022 | u8 | nombre d'échantillons (31 ou 11 ; non lu, le lecteur en prend 31) |
| 0x023 | u8 | nombre de motifs |
| 0x024 | u8 | nombre d'ordres |
| 0x025 | u8 | ordre de reprise (aucun s'il n'est pas inférieur au nombre d'ordres) |
| 0x026 | 256 | ordres (numéros de motifs) |
| 0x126 | 64 × 0x25 | échantillons : nom (22), finetune (0..15), volume (0..64), transposition (s8, en demi-tons), longueur, début et longueur de boucle (u32) |
| 0xA66 | n × 0x800 | motifs : 64 lignes de 8 cellules (note, échantillon, commande, paramètre) |
| — | — | données 8 bits signées de chaque échantillon non vide, dans l'ordre |

Les 20 fichiers ont exactement cette taille. Les longueurs tiennent sur 16 bits, ce que le lecteur suppose.

### Lecteur (`ASYLUM.DLL`, 1000:062D, 1000:03E6, 1000:092A)

`_ASS_PlayMusic` (1008:004D) copie les tables du module (31 échantillons, ordres, vitesse, tempo) et calcule la durée d'un tick : `fréquence / (tempo × 24 / 60)`. `_ASS_StartMusic` (1008:01AA) coupe les 8 canaux et repart du premier ordre. `_ASS_PauseMusic` suspend le lecteur et le mixage des canaux de musique (DS:0099).

À chaque tick (1000:092A), un compteur descend depuis la vitesse. Quand il arrive à zéro, la ligne suivante est lue (1000:062D, une cellule par canal) ; à chaque tick, celui de la ligne compris, les effets des canaux s'appliquent (1000:03E6). Arrivé à la ligne 64, le lecteur prend le motif de l'ordre suivant ; après le dernier, il revient à l'ordre de reprise. Sans ordre de reprise, il ne lit plus de lignes, mais les canaux continuent de sonner.

**Hauteur** : une période Amiga multipliée par 4. La note n joue `table[n + transposition] / C` : la table (DS:ED80, construite par 1018:084B) contient les 12 périodes de l'octave 0 (1712 … 907, DS:0123) multipliées par 16 × 8363 et divisées par 2 à chaque octave, sur 8 octaves ; C est la fréquence de do de l'échantillon selon son finetune (8363, 8413 … 8280, DS:00AB). Le pas du canal vaut `((0xDA7A68 / période) << 16) / fréquence de mixage`, où 0xDA7A68 est 4 fois l'horloge Amiga ; le décalage perd les bits hauts comme le registre 32 bits d'origine.

Une cellule avec un échantillon donne au canal le volume, les données, la transposition et la fréquence de do de cet échantillon, sans revenir au début. Une cellule avec une note fixe la période cible ; sauf avec l'effet 3, la période devient la cible et le son repart du début.

**Effets** (commande, paramètre xy) :

| Effet | À la ligne | À chaque tick |
|---|---|---|
| 0 arpège | cherche la note la plus proche de la période et prépare 3 pas : la note, +x, +y demi-tons | un des 3 pas, à tour de rôle |
| 1 / 2 | — | période − / + 4 × xy (au moins 0x71, pas de maximum) |
| 3 portamento | 0 : le dernier paramètre | vers la cible, de 4 × xy |
| 4 vibrato | x vitesse, y profondeur (0 : les dernières) ; position remise à 0 avec une note | position + 4x, écart `sinus × y >> 7` (table de 32 octets DS:026E, négatif sur la seconde moitié) ; période bornée à 0x71..0x6B00 pour le pas seulement |
| 5 / 6 | — | glissement de volume, puis 3 / 4 avec leurs derniers paramètres |
| 9 décalage | position xy × 256 (0 : le dernier), au plus longueur − 1 | — |
| A volume | — | − y puis + x, borné à 0..64, **aussi au tick de la ligne** |
| B saut d'ordre | ordre xy, fin du motif | — |
| C volume | au plus 64 | — |
| D cassure | ligne 10x + y, fin du motif | — |
| E9y / 1B redéclenchement | compteur y | au bout du compteur, retour au début |
| ECy coupure | compteur y | au bout du compteur, volume 0 (EC0 et EC1 coupent dès la ligne) |
| EAy / EBy | — | volume + / − y, une seule fois |
| F | vitesse (< 0x20) ou tempo : tick = `fréquence / (tempo × 0x66 >> 8)` | — |

Les autres effets sont ignorés par le lecteur : 7, 8 (panoramique, 629 fois dans les données ; la sortie est mono), E1/E2, E5, ED… Les canaux gardent leurs mémoires d'effets d'un morceau à l'autre.

**Bizarreries d'origine reproduites** :

- une cassure (D) fait compter les lignes depuis la ligne demandée, mais lit le nouveau motif depuis sa première ligne : le motif est raccourci au lieu d'être pris en cours ;
- le tempo initial (`× 24 / 60`, 441 échantillons pour 125) et celui de l'effet F (`× 0x66 >> 8`, 450) ne donnent pas la même durée de tick.

Les notes et arpèges qui sortiraient de la table la liraient au-delà ; le portage s'arrête à la dernière entrée (aucun cas dans les données).

### Morceaux du jeu (`STDINT.C`, 1200:00C8-0311)

| Piste | Fichier |
|---|---|
| 1 à 15 | `m01` à `m15` |
| 16 | `m16a`, `m16b`, `m16c` tour à tour : la lettre avance avant chaque chargement, la première fois donne `m16b` |
| 17 | `cred` (le générique) |
| 18 | `menu` (le menu) |
| 0, autres | silence |

`MusicProcess::I_playMusic` (1200:0143, intrinsic 0x012) arrête le morceau en cours, charge `SOUND/<nom>.amf` et le démarre, la musique en pause pendant le chargement. `I_stopMusic` (1200:0223, intrinsic 0x091) l'arrête. La piste en cours (DS:8624) et ses drapeaux (DS:3BFB, bit 0 : en cours) sont sauvegardés dans les entrées 70 et 71 ; charger une partie rejoue la piste depuis le début (1200:02BF).

Le menu (`IRGUMP_13d0_0000` / `03f5`) et le générique (`Credits_init` / `Credits_done`) jouent leur propre piste : 1200:0262 garde la piste en cours sur une pile de 4 (DS:861C), 1200:028E la rejoue **depuis le début** à la fermeture.

## Voix (`SNDMGR.C`)

12 voix sur les 12 canaux. Une voix (12 octets) contient : handle (u32, −1 si libre), son, propriétaire (0xFF pour l'interface, 0 pour les sources), volume, drapeaux (1 en boucle, 2 terminé).

- `sound_play` (12F0:0136) commence par `sound_update`, prend une voix (`sound_alloc_voice`), charge l'échantillon et appelle `_ASS_PlaySFX`. Le handle vient d'un compteur (DS:46F9), si bien qu'un ancien handle ne retrouve plus sa voix.
- `sound_alloc_voice` (12F0:0367) prend la première voix libre de la plage : 0-7 pour les sons ponctuels, 8-11 pour les sons en boucle. Si aucune n'est libre, elle reprend la voix pointée par l'index tournant de la plage (DS:46F5 / DS:46F7, qui avance ensuite). La source qui l'utilisait est libérée (`sound_source_release_handle`) et son canal est arrêté.
- `sound_update` (12F0:0428) libère les voix marquées terminées.
- `sound_retrigger` (12F0:026A) rejoue un son sur la voix d'un handle, sans boucle et à l'ancien volume ; si le handle a perdu sa voix, il joue sur une nouvelle.
- `sound_get_sfx` (12F0:057A, nommé `sound_get_volume` jusqu'ici) donne le son d'une voix, ou −1 quand il est fini. `sound_find_sfx` (12F0:05BC) donne le handle d'une voix qui joue un son donné.
- `sound_stop`, `sound_stop_all`, `sound_stop_sfx` (toutes les voix d'un son), `sound_set_volume`.

`AudioProcess::I_playSFX` (12E0:0476, intrinsics 0x0AB et 0x0D4) joue un son d'interface : volume 0x40, propriétaire 0xFF, sans boucle, quelle que soit la position. `AudioProcess::I_stopAllSFX` (0x0D1) arrête tout.

## Sources (`SNDSRC.C`)

Sons attachés à des objets, en 32 sources (`dsfx_init`) dont les 4 dernières sont réservées aux sons en boucle. Une source (11 octets) contient : handle, objet, son, volume, volume initial, en boucle.

Le volume vient de la place de l'objet (1450:0000) :

- 0x40 si l'objet est en partie à l'écran ;
- sinon, selon le plus grand de |dx| et |dy| jusqu'au joueur (DS:7E0E) :

| Distance | Volume |
|---|---|
| jusqu'à 0x17C | 0x32 |
| jusqu'à 0x280 | 0x20 |
| jusqu'à 0x3C0 | 0x10 |
| au-delà | 8 |

Seules les sources en boucle suivent ensuite le joueur (1450:0813, après chaque mouvement de caméra). Un son ponctuel garde son premier volume. Les numéros de son hors de 1..0x200 sont ignorés.

| Fonction | Intrinsic | Effet |
|---|---|---|
| `sound_source_play` 1450:021E | 0x015 `I_playSFXCru` | son ponctuel à l'objet, si une source est libre |
| `sound_source_play_unique` 02C5 | 0x02A `I_playAmbientSFXCru` | un son sans boucle joue comme ci-dessus ; un son en boucle seulement si aucune source ne le joue déjà (quel que soit l'objet) |
| `sound_source_retrigger` 03AD | 0x045 `I_playSFXCru` | rejoue le son de l'objet sur sa voix, ou le joue |
| `sound_source_stop` 0486 | 0x03B `I_stopSFXCru` | arrête la première source de l'objet qui joue ce son |
| `sound_source_stop_item` 0551 | 0x077 `I_stopSFXCru` | arrête toutes les sources de l'objet |
| `sound_source_is_playing` 0633 | 0x03D `I_isSFXPlayingForObject` | l'objet joue ce son |
| `sound_source_detach` 050B | — | `Item::I_destroy` d'un PNJ : ses sons continuent, détachés |
| `sound_source_stop_item_loops` 05B1 | — | `Item::I_destroy` : les sons en boucle de l'objet s'arrêtent |

## Sons joués par le moteur

| Où | Son |
|---|---|
| Image d'animation (`ActorAnim_step`, octet 3 de l'image) | rejoué sur la voix de l'acteur (pas, etc.) |
| Image de tir (`WPNCACHE_1160_005f`) | son de l'arme (`DTABLE` +2), rejoué sur la voix de l'acteur |
| Rechargement (`Actor_reload`, contrôleur) | son de rechargement de l'arme (+0xF) ; aussi quand le fusil à pompe est armé ; 0x2A `CLICK` sans chargeur |
| Impact (`impact_sprite`, 1150:02DF), en son d'interface | balles (types 1, 0xB, 0xD) : 0x26, 0x27, 0x41, 0x42, 0x45, 0x46 ; explosifs (3, 4, 10) : 0x1C, 0x6C, 0x3E ; électricité (0xF) : 0x48, 0x05 ; type 7 : tiré parmi 4 dans une table de 2, les deux suivants (formes 0x31C, 0x31F, au-delà de `SOUND.FLX`) ne jouent rien |
| Explosion d'objet (`Item_explode_ex`, 1150:0891) | arrête les sons de l'objet, puis 0x1C ou 0x6C joué au PNJ contrôlé, donc toujours à plein volume |
| Objet cassé (`ItemDamage_apply`) | octet 1 de son enregistrement `DAMAGE.FLX`, à l'objet |
| Bouclier du joueur qui arrête un tir | 0x48, rejoué sur le joueur |
| Blessure (humains ; le joueur aussi par le type 8) | 0x8F `DETHUM05`, 0xD8 `FW04` pour une femme (fiche +0x5A bit 2), si l'objet ne le joue pas déjà |
| Mort d'un PNJ | ses sons s'arrêtent (0x077) ; puis un cri pour un humain : électrocuté DS:273E (0xD9, 0xDA), homme DS:2738 (0x88, 0x8C, 0x8F), femme DS:2742 (0xD8, 0x10) |
| Mort du joueur | `DeathSilence` (type 0x261) : le son 9, 3 ticks plus tard |
| Atterrissage dur du joueur (`GravityProcess_dtor`) | 0x8F |
| Cris de combat (`Attack_default`) | par forme : tables DS:1FFD..2031 ; pendant qu'un cri joue, le PNJ ne vise ni ne tire |
| Supplications (`Surrender_run`) | par forme, dès que le PNJ voit le joueur : DS:1EA4 (0x2F7), 1EB0 (0x2F6), 1EB8 (0x344), 1EC0 (0x2F5), 1EC8 (0x597) |
| Stations de soin et de recharge (0x0F6, 0x0EE) | en boucle pendant le service : 0xDB `HEALING` / 0xA4 `FUSNCORE` ; à chaque tick : 0xBA `HEAL1` / 0x10B `RECHARGE` |

Les stations (`CruHealer`, `BatteryCharger`, 1130:1D70-2025, type 0x254, propriétaire le joueur, DS:7E10 levé pendant le service) :

- le soin monte les points de vie d'un par tick jusqu'au maximum (150 pour le joueur, `Actor_max_hp`) ;
- la recharge monte l'énergie de 25 par tick jusqu'au maximum de la batterie (`MainActor::I_getMaxEnergy` : 0, 2 500, 5 000 ou 10 000 selon DS:7E1F, octet +0x11 de l'entrée 20 des sauvegardes).

## Vérifié

- `audio_tests` :
  - le mixeur : volumes, demi-fréquence, boucle, arrêt en fin de son, filtre, démarrage au tampon suivant, tampon de 441 échantillons ;
  - le lecteur, sur un module synthétique : première ligne au 2e tick, période et pas d'une note, effet 1 dès le tick de la ligne, tempo de l'effet F (450), cassure qui raccourcit le motif, pause et arrêt ;
  - sur les vraies données : les 20 modules se chargent ; M01 (83 motifs, 90 ordres) joue 10 s avec du son et change de motif ; noms des pistes, tour de m16a/b/c, pile du menu ;
  - le tourniquet des voix, et la libération des voix finies ;
  - le volume selon la distance, et l'unicité des sons d'ambiance en boucle ;
  - sur les vraies données : 269 sons, `FOOT1` (0x113 octets à 11 025 Hz), la boucle de `FAN11`, les noms.
- `game_tests`, sur `SAVEGAME.13` :
  - le fusil à pompe joue `COCK1` en s'armant et `SHOTGN32` en tirant ;
  - un garde abattu pousse un cri de mort.
- `game_tests`, sur `SAVEGAME.1` : la musique de la sauvegarde (piste 1), celle du menu puis le retour à la piste 1, celle du générique puis le retour à celle du menu (fin normale ou touche).
- `game_tests`, sur `SAVEGAME.1` : le soin (PV 100 → 150) et la recharge (énergie 0 → 2 500), avec leurs sons, arrêtés à la fin.
- `crusader --png … --wav` sur les sauvegardes 1, 4 et 13 : les ambiances lancées par les scripts et les tirs s'entendent.

## Non reproduits

- La touche de débogage 0x432 (`WORLD_13e8_14b4`) qui fait défiler les pistes.
- Le panoramique des modules (l'original est mono aussi).
- Le pavé numérique (`KeypadGump`, segment 13C8, intrinsic 0x0C5) et ses sons : 0x3A / 0x3B pour les touches, 0x32 pour un code juste, 0x31 pour un code faux.
- Les sons d'interfaces absentes du portage :
  - les boîtes de dialogue d'Alt+X et des touches F (0x103 à 0x109) ;
  - la sélection d'objets (0xB0) ;
  - les bulles de texte (0x33).
- Les sons des morts spéciales (corps en flammes, `Actor_special_frame` : tables DS:2B12 / 2B1A), qui suivent ces morts.
- Le cache de sons, l'arrêt des sons à l'ouverture d'une cinématique, le clignotement de la barre d'état des stations.
- Dans le cas d'un PNJ mort qui atterrit, l'arrêt de son processus de type 0xF0.
