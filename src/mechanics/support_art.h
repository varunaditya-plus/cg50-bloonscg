#ifndef BLOONSCG_SUPPORT_ART_H
#define BLOONSCG_SUPPORT_ART_H

#include <stdint.h>

// Native support displays: https://github.com/KyleDerZweite/btd6-atlas/tree/ded3155921d70cd83d803b4d70022000e4c37c6b/data/56.3-build-24829026/game-data/Towers/EngineerMonkey
enum { ART_SENTRY = 0, ART_TRAP_EMPTY = 8, ART_TRAP_FULL = 9, ART_BANANA = 10 };

static const uint8_t support_bounds[][4] = {
    {4, 5, 9, 8},
    {4, 4, 8, 9},
    {4, 5, 8, 8},
    {4, 4, 8, 9},
    {3, 5, 9, 8},
    {4, 4, 8, 9},
    {4, 3, 8, 10},
    {4, 4, 8, 9},
    {3, 2, 10, 12},
    {2, 3, 12, 9},
    {4, 4, 7, 7},
};

#endif
