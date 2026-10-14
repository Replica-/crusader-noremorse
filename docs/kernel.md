# Noyau de processus

Analyse de `KERNEL.C` et `PROCESS.C`, et correspondance avec la réimplémentation (`src/engine/`).

## Principe

Tout ce qui dure dans le temps est un **processus coopératif** : l'horloge, les démons d'entrée, le mixage du son, les fondus, les scripts usecode, les animations des personnages. Le noyau tient une table indexée par identifiant (`pid`) ; à chaque tranche, il la parcourt dans l'ordre et appelle `run()` sur chaque processus exécutable.

Un processus est exécutable s'il n'est ni terminé, ni suspendu, ni endormi, ni en attente, et que son **niveau** est le niveau courant du noyau. Les processus marqués « tout niveau » et les niveaux système 30 et 31 tournent toujours.

## Niveaux (modalité)

Un processus créé prend le niveau courant. `raise_level` le fait monter, avec le noyau, d'un cran : tout ce qui est en dessous se fige, comme le monde du jeu derrière un menu. Quand plus aucun processus n'occupe le niveau courant, le noyau redescend à la fin de la tranche. Le portage, qui figure le menu et la boîte de message par un processus muet élevé d'un niveau (l'original les fait tourner dans une boucle modale hors du noyau), redescend dès leur fermeture (`drop_empty_levels`) : sinon un processus créé entre-temps, comme l'animation lancée au relâchement d'une touche, resterait au niveau élevé.

## Attentes

Un processus peut attendre la fin d'autres processus. Dans l'original, l'attente est une **expression en notation polonaise inverse** stockée dans une liste. Elle contient des pids et des opérateurs : `0x401` (pid terminé), `0x402` (ET), `0x404` (OR), `0x408` (fin de liste). À chaque fin d'un processus attendu, l'expression est réévaluée ; quand elle devient vraie, l'attente cesse.

Le processus réveillé récupère le **résultat** du processus qui l'a réveillé ; c'est l'opcode usecode `0x6D`, « push process result ». Une fin peut être normale (`terminate`) ou un échec (`fail`, drapeau `0x20`).

## Temps

Dans l'original, le processus `SystemTimer` compte des ticks à **60 Hz**. Les `TimedProcess` (horloge, `KeyDaemon` à 45 Hz, mixage…) cumulent le temps écoulé à chaque image et s'exécutent **au plus une fois par image** quand leur période est atteinte ; le reste est reporté.

## Correspondance avec la réimplémentation

| Original | Portage |
|---|---|
| table de pointeurs `far` + compteurs par niveau | `Kernel::table_`, `per_level_` |
| `Kernel_run_slice` | `Kernel::run_slice` |
| `Process_sleep` / `Process_check_sleep` | `Process::sleep`, réveil dans `run_slice` |
| liste d'attente RPN (`+1E`) | `Process::wait_expr_` (jetons typés au lieu de codes `0x40x`) |
| relations `0x2000` / `0x4000` (`+28`) | `wait_expr_` / `waiters_` |
| `Process_terminate_with_result`, `Process_fail` | `terminate`, `fail` |
| `Process_raise_level` | `Kernel::raise_level` |
| `SystemTimer` (60 Hz) | compteur de ticks du noyau, alimenté par `advance_time` |
| `TimedProcess_accumulate` | `TimedProcess::accumulate` (au plus un tick par image) |
| `Kernel_I_getNumProcesses` | `Kernel::count_processes` ; le type `0x0C` vaut « tout type » (`Kernel::kAnyType`, comparaison `CMP [BP+8],0xC`) |
| `Kernel_fail_processes` (`Kernel_I_resetRef` avec le masque de priorité `0x21` = niveau 1 exactement) | `Kernel::fail_processes` |

## Écarts connus, à reprendre

- Les relations « écouteur » (`0x800` / `0x1000`, `MessageProcess`), les processus d'interruption (`STDINT`) et l'abonnement clavier (`Process_want_keys`) ne sont pas encore portés.
- La suspension par type (`Process_suspend_by_type`) n'est pas un mécanisme général du noyau du portage : les animations d'acteur (type `0xF0`) la reproduisent en attendant la dernière animation en file (`do_anim`). Les compteurs par objet pour les types `0xF0`–`0xF7` ne sont pas portés.
- `Kernel_fail_processes` commence à l'emplacement 3 de la table, ignore les processus portant le drapeau `0x200` (en `+0x18`, sens inconnu) et consulte d'abord un tableau de bits « l'objet a des processus » (`3A8C`) ; rien de cela n'est reproduit.
- La sauvegarde et le chargement de la table de processus (format des parties sauvegardées) restent à analyser.
