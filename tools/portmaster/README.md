# Portage PortMaster (consoles aarch64 : RG40XX-H, etc.)

Compile `crusader` pour Linux aarch64 (glibc ≥ 2.28) depuis Windows ou Linux, avec Zig comme compilateur croisé. Le binaire utilise le SDL2 du système de la console (muOS, Knulli, ROCKNIX, ArkOS…) ; libc++ est liée statiquement.

## Compiler

```
pip install ziglang ninja
python tools/portmaster/build.py          # -> build-portmaster/crusader
```

`build.py` récupère les en-têtes SDL2, fabrique une `libSDL2-2.0.so.0` vide (symboles seulement) pour l'édition de liens, puis lance CMake avec `zig-aarch64.cmake`.

## Installer sur la console

Sur la carte qui contient `ports/` et `ROMS/Ports/` (SD2 sous muOS) :

```
ports/crusader/crusader        le binaire
ports/crusader/crusader.gptk   gptokeyb : seulement la combinaison pour quitter
ports/crusader/data/           CRUSADER.EXE, CRUSADER.CFG, ASYLUM.DLL, IDATA, USECODE, SAVEGAME, SOUND, STATIC, FLICS
ROMS/Ports/Crusader.sh         lanceur (Crusader.sh)
```

Les données sont celles du jeu d'origine (version française), à fournir soi-même. Le jeu écrit ses sauvegardes et `CRUSADER.CFG` dans `data/`.

## Manette

Le jeu lit la manette lui-même (SDL_GameController) : `crusader.gptk` ne mappe aucun bouton, `gptokeyb` ne sert qu'à quitter (hotkey + start). Les réglages par défaut sont ceux de l'écran « MANETTE » des options, où chaque action se réassigne (écrits dans `data/CRUSADER.CFG`, clés `padmap`, `padaim`, `paddeadzone`) :

| Entrée | Action |
|---|---|
| Stick gauche | flèches (déplacement, rotation) |
| Stick droit haut / bas | visée (souris) |
| Stick droit gauche / droite | roulade gauche / droite (Ctrl + gauche / droite) |
| RT | tir |
| A / B / X / Y | sélectionner (S) / saut / valider / arme suivante |
| LB / RB | pas à gauche / à droite (Alt + gauche / droite) |
| L3 / R3 | Maj / se baisser ou se lever (Ctrl + bas) |
| Croix | utiliser l'objet / trousse / objet précédent / objet suivant |
| Start | menu |

Dans les menus : croix = flèches, stick droit = souris, A = Entrée, B et Start = Échap.
