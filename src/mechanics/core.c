#include "game.h"
#include "animations.h"
#include <string.h>

#ifndef __sh__
Game game;
#endif
extern const TowerDef dart_monkey_def;
extern const TowerDef boomerang_monkey_def;
extern const TowerDef bomb_shooter_def;
extern const TowerDef tack_shooter_def;
extern const TowerDef ice_monkey_def;
extern const TowerDef glue_gunner_def;
extern const TowerDef sniper_monkey_def;
extern const TowerDef monkey_ace_def;
extern const TowerDef heli_pilot_def;
extern const TowerDef mortar_monkey_def;
extern const TowerDef dartling_gunner_def;
extern const TowerDef wizard_monkey_def;
extern const TowerDef super_monkey_def;
extern const TowerDef ninja_monkey_def;
extern const TowerDef alchemist_def;
extern const TowerDef druid_def;
extern const TowerDef banana_farm_def;
extern const TowerDef spike_factory_def;
extern const TowerDef monkey_village_def;
extern const TowerDef engineer_monkey_def;
const TowerDef *tower_defs[MONKEY_COUNT] = {
    [0] = &dart_monkey_def,     [1] = &boomerang_monkey_def, [2] = &bomb_shooter_def,
    [3] = &tack_shooter_def,    [4] = &ice_monkey_def,       [5] = &glue_gunner_def,
    [6] = &sniper_monkey_def,   [7] = &monkey_ace_def,       [8] = &heli_pilot_def,
    [9] = &mortar_monkey_def,   [10] = &dartling_gunner_def, [11] = &wizard_monkey_def,
    [12] = &super_monkey_def,   [13] = &ninja_monkey_def,    [14] = &alchemist_def,
    [15] = &druid_def,          [16] = &banana_farm_def,     [17] = &spike_factory_def,
    [18] = &monkey_village_def, [19] = &engineer_monkey_def,
};
extern const uint8_t meadow_placement[];

// Footprints fit the main body or platform; narrow top details can overhang paths.
// Oval rasterization: https://gitea.planet-casio.com/Lephenixnoir/gint/src/branch/master/src/render/dellipse.c
const PlacementShape placement_shapes[MONKEY_COUNT] = {
    [0] = {.x = -4, .y = -9, .rows = {
        0x3c00, 0x7e00, 0x7e00, 0xff00, 0xff00, 0xff00, 0xff00, 0x7e00,
        0x7e00, 0x3c00,
    }},
    [1] = {.x = -4, .y = -9, .rows = {
        0x1c00, 0x3e00, 0x7f00, 0xff80, 0xff80, 0xff80, 0xff80, 0xff80,
        0xff80, 0x7f00, 0x3e00, 0x1c00,
    }},
    [2] = {.x = -4, .y = -5, .rows = {
        0x3c00, 0x7e00, 0xff00, 0xff00, 0xff00, 0xff00, 0x7e00, 0x3c00,
    }},
    [3] = {.x = -7, .y = -7, .rows = {
        0x0fc0, 0x1fe0, 0x3ff0, 0x7ff8, 0xfffc, 0xfffc, 0xfffc, 0xfffc,
        0xfffc, 0x7ff8, 0x3ff0, 0x1fe0, 0x0fc0,
    }},
    [4] = {.x = -5, .y = -12, .rows = {
        0x1e00, 0x3f00, 0x7f80, 0x7f80, 0xffc0, 0xffc0, 0xffc0, 0xffc0,
        0x7f80, 0x7f80, 0x3f00, 0x1e00,
    }},
    [5] = {.x = -4, .y = -10, .rows = {
        0x1800, 0x3c00, 0x7e00, 0x7e00, 0xff00, 0xff00, 0xff00, 0xff00,
        0xff00, 0x7e00, 0x7e00, 0x3c00, 0x1800,
    }},
    [6] = {.x = -3, .y = -7, .rows = {
        0x3000, 0x7800, 0xfc00, 0xfc00, 0xfc00, 0xfc00, 0x7800, 0x3000,
    }},
    [7] = {.x = -8, .y = -6, .rows = {
        0x7ffe, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
        0xffff, 0x7ffe,
    }},
    [8] = {.x = -8, .y = -8, .rows = {
        0x7ffe, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
        0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0x7ffe,
    }},
    [9] = {.x = -6, .y = -7, .rows = {
        0x3f00, 0x7f80, 0xffc0, 0xffc0, 0xffc0, 0xffc0, 0xffc0, 0xffc0,
        0x7f80, 0x3f00,
    }},
    [10] = {.x = -3, .y = -7, .rows = {
        0x3000, 0x7800, 0xfc00, 0xfc00, 0xfc00, 0xfc00, 0xfc00, 0xfc00,
        0x7800, 0x3000,
    }},
    [11] = {.x = -4, .y = -9, .rows = {
        0x1800, 0x3c00, 0x7e00, 0xff00, 0xff00, 0xff00, 0xff00, 0xff00,
        0x7e00, 0x3c00, 0x1800,
    }},
    [12] = {.x = -5, .y = -8, .rows = {
        0x1f00, 0x3f80, 0x7fc0, 0xffe0, 0xffe0, 0xffe0, 0xffe0, 0x7fc0,
        0x3f80, 0x1f00,
    }},
    [13] = {.x = -5, .y = -8, .rows = {
        0x1e00, 0x3f00, 0x7f80, 0xffc0, 0xffc0, 0xffc0, 0xffc0, 0xffc0,
        0x7f80, 0x3f00, 0x1e00,
    }},
    [14] = {.x = -4, .y = -10, .rows = {
        0x1800, 0x3c00, 0x7e00, 0xff00, 0xff00, 0xff00, 0xff00, 0xff00,
        0xff00, 0x7e00, 0x3c00, 0x1800,
    }},
    [15] = {.x = -4, .y = -10, .rows = {
        0x1800, 0x3c00, 0x7e00, 0xff00, 0xff00, 0xff00, 0xff00, 0xff00,
        0x7e00, 0x3c00, 0x1800,
    }},
    [16] = {.x = -6, .y = -4, .rows = {
        0x7fe0, 0xfff0, 0xfff0, 0xfff0, 0xfff0, 0xfff0, 0xfff0, 0xfff0,
        0x7fe0,
    }},
    [17] = {.x = -8, .y = -7, .rows = {
        0x3ffc, 0x7ffe, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
        0xffff, 0xffff, 0xffff, 0x7ffe, 0x3ffc,
    }},
    [18] = {.x = -5, .y = -9, .rows = {
        0x1f00, 0x3f80, 0x7fc0, 0xffe0, 0xffe0, 0xffe0, 0xffe0, 0xffe0,
        0x7fc0, 0x3f80, 0x1f00,
    }},
    [19] = {.x = -4, .y = -9, .rows = {
        0x1c00, 0x3e00, 0x7f00, 0xff80, 0xff80, 0xff80, 0xff80, 0xff80,
        0xff80, 0x7f00, 0x3e00, 0x1c00,
    }},
};

// Q4 squared distances avoid 64-bit division in collision loops.
int32_t distance_squared(int32_t x, int32_t y, int32_t a, int32_t b)
{
    int32_t dx = (x - a) / 16, dy = (y - b) / 16;
    return dx * dx + dy * dy;
}

uint32_t integer_sqrt(uint32_t n)
{
    uint32_t result = 0, bit = 1u << 30;
    while (bit > n)
        bit >>= 2;
    while (bit) {
        if (n >= result + bit) {
            n -= result + bit;
            result = (result >> 1) + bit;
        } else
            result >>= 1;
        bit >>= 2;
    }
    return result;
}

// Calibrated against five road centres in references/meadow-guide.png.
int world_x(int x)
{
    return (x * Q - 42624) * 25 / 27;
}

int world_y(int y)
{
    return (y * Q - 27456) * 15 / 14;
}

int screen_x(int x)
{
    int n = x * 27 / 25 + 42624;
    return (n + (n < 0 ? -128 : 128)) / Q;
}

int screen_y(int y)
{
    int n = y * 14 / 15 + 27456;
    return (n + (n < 0 ? -128 : 128)) / Q;
}

// Figma fills the viewport from the original map crop at x=5, scaled by 326/315.
int map_x(int world)
{
    int n = (screen_x(world) - 5) * 326;
    return (n + (n < 0 ? -157 : 157)) / 315;
}

int map_y(int world)
{
    int n = screen_y(world) * 326 - 2915;
    return (n + (n < 0 ? -157 : 157)) / 315;
}

int terrain_x(int x)
{
    return 5 + (x * 315 + 157) / 326;
}

int terrain_y(int y)
{
    return (y * 315 + 3078) / 326;
}

uint32_t game_random(void)
{
    // Xorshift32: https://www.jstatsoft.org/article/view/v008i14
    uint32_t x = game.support_random ? game.support_random : 0x7f4a7c15u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    game.support_random = x;
    return x;
}

void game_init(void)
{
    memset(&game, 0, sizeof game);
    bloons_reset_cache();
    combat_reset_cache();
    shots_init();
    animations_init();
    necromancy_reset_cache();
    // Medium Standard: https://github.com/KyleDerZweite/btd6-atlas/tree/ded3155921d70cd83d803b4d70022000e4c37c6b
    game.cash = 65000;
    game.lives = 150;
    game.speed = 1;
}

const TowerProfile *tower_profile(const Tower *t)
{
    const TowerDef *d = tower_defs[t->type];
    return d && t->profile < d->profile_count ? &d->profiles[t->profile] : NULL;
}

const Upgrade *tower_next_upgrade(const Tower *t, unsigned p)
{
    const TowerDef *d = tower_defs[t->type];
    const TowerProfile *s = tower_profile(t);
    if (!d || !s || p >= 3 || s->tiers[p] >= d->caps[p])
        return NULL;
    return &d->upgrades[p][s->tiers[p]];
}

static int placement_overlaps(unsigned type, int x, int y, const Tower *other)
{
    const PlacementShape *a = &placement_shapes[type];
    const PlacementShape *b = &placement_shapes[other->type];
    int dx = map_x(other->x) + b->x - (x + a->x);
    int dy = map_y(other->y) + b->y - (y + a->y);

    if (dx <= -16 || dx >= 16 || dy <= -16 || dy >= 16)
        return 0;

    for (int row = 0; row < 16; row++) {
        int other_row = row - dy;
        if (other_row < 0 || other_row >= 16)
            continue;

        uint32_t bits = b->rows[other_row];
        bits = dx < 0 ? bits << -dx : bits >> dx;
        if (a->rows[row] & bits)
            return 1;
    }
    return 0;
}

int can_place_monkey(unsigned type, int x, int y, int ignore)
{
    if (type >= MONKEY_COUNT || !tower_defs[type] ||
        (game.tower_count >= TOWER_LIMIT && ignore < 0))
        return 0;

    const PlacementShape *shape = &placement_shapes[type];
    int px = map_x(world_x(x));
    int py = map_y(world_y(y));

    for (int row = 0; row < 16; row++) {
        uint32_t bits = shape->rows[row];
        if (!bits)
            continue;

        int yy = py + shape->y + row;
        if (yy < 0 || yy >= 205)
            return 0;
        int source_y = terrain_y(yy);

        while (bits) {
            int col = 15 - __builtin_ctz(bits);
            bits &= bits - 1;
            int xx = px + shape->x + col;
            if (xx < 0 || xx >= 326)
                return 0;

            int source_x = terrain_x(xx);
            if (!(meadow_placement[source_y * 48 + source_x / 8] &
                  (0x80 >> (source_x % 8))))
                return 0;
        }
    }

    for (unsigned id = 0; id < TOWER_LIMIT; id++) {
        const Tower *other = &game.towers[id];
        if (!other->active || id == (unsigned)ignore)
            continue;
        if (placement_overlaps(type, px, py, other))
            return 0;
    }
    return 1;
}

int game_place(unsigned type, int x, int y)
{
    if (!can_place_monkey(type, x, y, -1))
        return -1;
    unsigned price = tower_defs[type]->price * 100u;
    if (game.cash < price)
        return -1;

    for (int i = 0; i < TOWER_LIMIT; i++)
        if (!game.towers[i].active) {
            Tower *t = &game.towers[i];
            memset(t, 0, sizeof *t);
            t->x = world_x(x);
            t->y = world_y(y);
            t->aim_x = t->x;
            t->aim_y = t->y;
            t->air_x = t->x;
            t->air_y = t->y;
            t->type = type;
            t->active = 1;
            t->spent = price;
            game.cash -= price;
            game.tower_count++;
            tower_animations[i] = (TowerAnimation){.facing = 2};
            necromancy_reset_cache();
            return i;
        }
    return -1;
}

int game_upgrade_allowed(unsigned id, unsigned path)
{
    if (id >= TOWER_LIMIT || !game.towers[id].active || path >= 3)
        return 0;
    Tower *t = &game.towers[id];
    const TowerProfile *s = tower_profile(t);

    if (!s || !tower_next_upgrade(t, path))
        return 0;
    unsigned nonzero = 0, high = 0;

    for (int p = 0; p < 3; p++) {
        unsigned n = s->tiers[p] + (p == (int)path);
        nonzero += n > 0;
        high += n > 2;
    }

    if (nonzero > 2 || high > 1)
        return 0;
    const TowerDef *definition = tower_defs[t->type];

    for (unsigned i = 0; i < definition->profile_count; i++) {
        int match = 1;
        for (unsigned p = 0; p < 3; p++)
            if (definition->profiles[i].tiers[p] != s->tiers[p] + (p == path))
                match = 0;
        if (match)
            return 1;
    }
    return 0;
}

int game_upgrade(unsigned id, unsigned path)
{
    if (!game_upgrade_allowed(id, path))
        return 0;
    Tower *t = &game.towers[id];
    const Upgrade *u = tower_next_upgrade(t, path);

    if (game.cash < u->price * 100u)
        return 0;
    const TowerDef *d = tower_defs[t->type];
    const TowerProfile *old = tower_profile(t);

    for (unsigned i = 0; i < d->profile_count; i++) {
        int match = 1;
        for (int p = 0; p < 3; p++)
            if (d->profiles[i].tiers[p] != old->tiers[p] + (p == (int)path))
                match = 0;
        if (match) {
            t->profile = i;
            necromancy_reset_cache();
            t->spent += u->price * 100u;
            game.cash -= u->price * 100u;
            return 1;
        }
    }
    return 0;
}

void game_sell(unsigned id)
{
    if (id >= TOWER_LIMIT || !game.towers[id].active)
        return;
    Tower *t = &game.towers[id];
    game.cash += t->spent * 7 / 10;
    t->active = 0;
    game.tower_count--;
    shots_owner_destroy(id);
    necromancy_reset_cache();
}

const RoundDef *game_round(void)
{
    unsigned index = game.round ? game.round - 1 : 0;
    if (index >= 60) {
        // Freeplay cycles rounds 51-60 without growing the spawn buffers.
        index = 50 + (index - 60) % 10;
    }
    return &rounds[index];
}

void game_start_round(void)
{
    if (game.running || game.lost)
        return;
    game.won = 0;
    game.round++;
    game.round_time = 0;
    game.running = 1;
    memset(game.group_spawned, 0, sizeof game.group_spawned);
}

void game_tick(void)
{
    if (game.won || game.lost)
        return;
    game.time += TICK;
    animations_tick();
    necromancy_begin_tick();
    rounds_tick();
    towers_tick();
    support_units_tick();
    shots_tick();
    bloons_tick();
}
