cmake_minimum_required(VERSION 3.21)
project(crusader_port LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# SDL2: use the system package when available (Linux distros), otherwise build it.
find_package(SDL2 CONFIG QUIET)
if(NOT SDL2_FOUND)
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL2
    URL https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz)
  FetchContent_MakeAvailable(SDL2)
endif()
if(TARGET SDL2::SDL2-static)
  set(SDL2_TARGET SDL2::SDL2-static)
else()
  set(SDL2_TARGET SDL2::SDL2)
endif()

# Game data formats, no platform dependency.
add_library(crusader_core
  src/core/flex.cpp
  src/core/palette.cpp
  src/core/shape.cpp
  src/core/xform.cpp
  src/core/weapon_overlay.cpp
  src/core/font.cpp
  src/core/game_config.cpp
  src/core/dtable.cpp
  src/core/typeflags.cpp
  src/core/trig.cpp
  src/core/anim_dat.cpp
  src/core/sound_flex.cpp
  src/core/amf_module.cpp
  src/core/png_writer.cpp
  src/core/avi_movie.cpp
  src/core/movie_subtitles.cpp)
target_include_directories(crusader_core PUBLIC src)

# Engine core: process kernel (KERNEL.C / PROCESS.C), no platform dependency.
add_library(crusader_engine
  src/engine/kernel.cpp
  src/engine/process.cpp
  src/engine/palette_fader.cpp
  src/engine/usecode/usecode_file.cpp
  src/engine/usecode/uc_heap.cpp
  src/engine/usecode/uc_process.cpp
  src/engine/usecode/uc_machine.cpp
  src/engine/usecode/item_intrinsics.cpp
  src/engine/usecode/world_intrinsics.cpp
  src/engine/usecode/npc_intrinsics.cpp
  src/engine/usecode/audio_intrinsics.cpp
  src/engine/usecode/movie_intrinsics.cpp
  src/engine/audio/asylum_mixer.cpp
  src/engine/audio/asylum_music.cpp
  src/engine/audio/music.cpp
  src/engine/audio/sound_manager.cpp
  src/engine/audio/sound_sources.cpp
  src/engine/world/world.cpp
  src/engine/world/item_search.cpp
  src/engine/world/actor_anim.cpp
  src/engine/world/collision.cpp
  src/engine/world/avatar_mover.cpp
  src/engine/world/gravity.cpp
  src/engine/world/combat_control.cpp
  src/engine/world/projectile.cpp
  src/engine/world/attack.cpp
  src/engine/world/activity.cpp
  src/engine/world/actor_create.cpp
  src/engine/world/pathfind.cpp
  src/engine/world/player_commands.cpp
  src/engine/world/item_damage.cpp
  src/engine/world/chargers.cpp
  src/engine/world/inventory.cpp
  src/engine/world/item_selection.cpp
  src/engine/world/map_change.cpp
  src/engine/world/teleport.cpp
  src/engine/world/sprite_process.cpp
  src/engine/world/aim_mode.cpp
  src/engine/world/world_mouse.cpp
  src/engine/world/palette_cycle.cpp
  src/engine/world/pickup_list.cpp
  src/engine/input/keyboard.cpp
  src/engine/input/mouse_queue.cpp
  src/engine/ui/status_bar.cpp
  src/engine/ui/pickup_area.cpp
  src/engine/ui/computer_gump.cpp
  src/engine/ui/keypad_gump.cpp
  src/engine/ui/mouse_cursor.cpp
  src/engine/ui/main_menu.cpp
  src/engine/ui/load_screen.cpp
  src/engine/ui/options_screen.cpp
  src/engine/ui/difficulty_screen.cpp
  src/engine/ui/message_gump.cpp
  src/engine/ui/shape_dialog.cpp
  src/engine/ui/help_screen.cpp
  src/engine/ui/credits.cpp
  src/engine/ui/movie_player.cpp
  src/engine/world/render.cpp)
target_link_libraries(crusader_engine PUBLIC crusader_core)

# Platform layer (replaces SUPERVGA/VPORT/BASEPAL hardware access).
add_library(crusader_platform
  src/platform/sdl_presenter.cpp
  src/platform/sdl_input.cpp
  src/platform/sdl_audio.cpp)
target_link_libraries(crusader_platform PUBLIC crusader_engine ${SDL2_TARGET})

# The game without platform code (testable headless), and its executable.
add_library(crusader_game src/game/game.cpp)
target_link_libraries(crusader_game PUBLIC crusader_engine)

add_executable(crusader src/game/main.cpp)
target_link_libraries(crusader PRIVATE crusader_game crusader_platform)
# Set by the release pipeline (semantic version, without the leading "v").
set(CRUSADER_VERSION "0.0.0-dev" CACHE STRING "Version reported by crusader --version")
configure_file(src/game/version.hpp.in ${CMAKE_BINARY_DIR}/generated/crusader_version.hpp @ONLY)
target_include_directories(crusader PRIVATE ${CMAKE_BINARY_DIR}/generated)

add_executable(shapeview src/tools/shapeview.cpp)
target_link_libraries(shapeview PRIVATE crusader_platform)

add_executable(mapview src/tools/mapview.cpp)
target_link_libraries(mapview PRIVATE crusader_engine crusader_platform)

add_executable(ucsmoke src/tools/ucsmoke.cpp)
target_link_libraries(ucsmoke PRIVATE crusader_engine)

add_executable(diffcheck src/tools/diffcheck.cpp)
target_link_libraries(diffcheck PRIVATE crusader_engine)

add_executable(useprobe src/tools/useprobe.cpp)
target_link_libraries(useprobe PRIVATE crusader_game)

add_executable(shapedump src/tools/shapedump.cpp)
target_link_libraries(shapedump PRIVATE crusader_core)

enable_testing()
add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE crusader_core)
add_test(NAME core_tests COMMAND core_tests ${CMAKE_SOURCE_DIR}/data)

add_executable(kernel_tests tests/kernel_tests.cpp)
target_link_libraries(kernel_tests PRIVATE crusader_engine)
add_test(NAME kernel_tests COMMAND kernel_tests)

add_executable(input_tests tests/input_tests.cpp)
target_link_libraries(input_tests PRIVATE crusader_platform)
add_test(NAME input_tests COMMAND input_tests)

add_executable(usecode_tests tests/usecode_tests.cpp)
target_link_libraries(usecode_tests PRIVATE crusader_engine)
add_test(NAME usecode_tests COMMAND usecode_tests)

add_executable(world_tests tests/world_tests.cpp)
target_link_libraries(world_tests PRIVATE crusader_engine)
add_test(NAME world_tests COMMAND world_tests ${CMAKE_SOURCE_DIR}/data)

add_executable(game_tests tests/game_tests.cpp)
target_link_libraries(game_tests PRIVATE crusader_game)
add_test(NAME game_tests COMMAND game_tests ${CMAKE_SOURCE_DIR}/data)

add_executable(audio_tests tests/audio_tests.cpp)
target_link_libraries(audio_tests PRIVATE crusader_engine)
add_test(NAME audio_tests COMMAND audio_tests ${CMAKE_SOURCE_DIR}/data)

# Every C++ function names the original routine it rewrites (tools/orig_map.py).
# The differential checks run original routines in an emulator (Unicorn).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND AND EXISTS ${CMAKE_SOURCE_DIR}/tools/orig_map.py)
    add_test(NAME orig_map COMMAND Python3::Interpreter -I ${CMAKE_SOURCE_DIR}/tools/orig_map.py --check)
    execute_process(COMMAND ${Python3_EXECUTABLE} -c "import unicorn" RESULT_VARIABLE NO_UNICORN
                    OUTPUT_QUIET ERROR_QUIET)
    if(NO_UNICORN EQUAL 0 AND EXISTS ${CMAKE_SOURCE_DIR}/tools/diffcheck/run.py)
        add_test(NAME diffcheck COMMAND Python3::Interpreter -I ${CMAKE_SOURCE_DIR}/tools/diffcheck/run.py
                 --exe $<TARGET_FILE:diffcheck> --data ${CMAKE_SOURCE_DIR}/data)
    else()
        message(STATUS "Python module unicorn missing: differential checks not registered")
    endif()
endif()
