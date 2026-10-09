cmake_minimum_required(VERSION 3.15)
project(BloonsCG LANGUAGES C)

list(APPEND CMAKE_MODULE_PATH "$ENV{HOME}/.local/lib/cmake/fxsdk")

include(GenerateG3A)
include(Fxconv)
find_package(Gint 2.9 REQUIRED)

set(ASSETS
  assets-cg/meadow.png
  assets-cg/monkey-portraits.png
  assets-cg/monkey-sprites.png
  assets-cg/meadow-placement.bin
  assets-cg/bloons.png
  assets-cg/upgrade-icons.png
)
fxconv_declare_assets(${ASSETS} WITH_METADATA)

add_executable(bloonscg
  src/main.c
  src/mechanics/core.c
  src/mechanics/combat.c
  src/mechanics/shots.c
  src/mechanics/collisions.c
  src/mechanics/lightning.c
  src/mechanics/projectiles.c
  src/mechanics/traps.c
  src/mechanics/air.c
  src/mechanics/support.c
  src/mechanics/bloons.c
  src/mechanics/world.c
  src/mechanics/ui.c
  src/monkeys/dart_monkey.c
  src/monkeys/boomerang_monkey.c
  src/monkeys/bomb_shooter.c
  src/monkeys/tack_shooter.c
  src/monkeys/ice_monkey.c
  src/monkeys/glue_gunner.c
  src/monkeys/sniper_monkey.c
  src/monkeys/monkey_ace.c
  src/monkeys/heli_pilot.c
  src/monkeys/mortar_monkey.c
  src/monkeys/dartling_gunner.c
  src/monkeys/wizard_monkey.c
  src/monkeys/super_monkey.c
  src/monkeys/ninja_monkey.c
  src/monkeys/alchemist.c
  src/monkeys/druid.c
  ${ASSETS}
)
target_compile_options(bloonscg PRIVATE -Wall -Wextra -Os)
target_include_directories(bloonscg PRIVATE src)
target_link_libraries(bloonscg Gint::Gint)

generate_g3a(
  TARGET bloonscg
  OUTPUT "${CMAKE_SOURCE_DIR}/BloonsCG.g3a"
  NAME "Bloons CG"
  ICONS
    "${CMAKE_SOURCE_DIR}/assets-cg/icon-uns.png"
    "${CMAKE_SOURCE_DIR}/assets-cg/icon-sel.png"
)
