#include "game.h"
#include <string.h>

#ifndef __sh__
Game game;
#endif
extern const TowerDef dart_monkey_def;
extern const TowerDef boomerang_monkey_def;
extern const TowerDef bomb_shooter_def;
extern const TowerDef tack_shooter_def;
extern const TowerDef ice_monkey_def;
const TowerDef *tower_defs[MONKEY_COUNT] = {
    [0] = &dart_monkey_def,  [1] = &boomerang_monkey_def, [2] = &bomb_shooter_def,
    [3] = &tack_shooter_def, [4] = &ice_monkey_def,
};

const char *const monkey_names[MONKEY_COUNT] = {
    "Dart Monkey",     "Boomerang Monkey", "Bomb Shooter",  "Tack Shooter",   "Ice Monkey",
    "Glue Gunner",     "Sniper Monkey",    "Monkey Ace",    "Heli Pilot",     "Mortar Monkey",
    "Dartling Gunner", "Wizard Monkey",    "Super Monkey",  "Ninja Monkey",   "Alchemist",
    "Druid",           "Banana Farm",      "Spike Factory", "Monkey Village", "Engineer Monkey"};
const uint16_t monkey_prices[MONKEY_COUNT] = {200, 315, 375,  260, 400, 225, 350,  800,  1600, 750,
                                              850, 375, 2500, 500, 550, 400, 1250, 1000, 1200, 400};

extern const uint8_t meadow_placement[];

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
    combat_reset_cache();
    shots_init();
    // Medium Standard:
    // https://github.com/KyleDerZweite/btd6-atlas/tree/ded3155921d70cd83d803b4d70022000e4c37c6b
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

typedef struct {
    int32_t x, y;
    unsigned rectangle;
} PlacementFootprint;
static PlacementFootprint placement_footprint(const TowerProfile *p)
{
    if (p->footprint_rectangle)
        return (PlacementFootprint){p->footprint_x / 2, p->footprint_y / 2, 1};
    return (PlacementFootprint){p->footprint, p->footprint, 0};
}

static int placement_overlaps(PlacementFootprint a, int32_t ax, int32_t ay, PlacementFootprint b,
                              int32_t bx, int32_t by)
{
    int32_t dx = ax > bx ? ax - bx : bx - ax, dy = ay > by ? ay - by : by - ay;
    if (a.rectangle && b.rectangle)
        return dx < a.x + b.x && dy < a.y + b.y;
    if (!a.rectangle && !b.rectangle) {
        int32_t radius = a.x + b.x;
        return (int64_t)dx * dx + (int64_t)dy * dy < (int64_t)radius * radius;
    }

    if (!a.rectangle) {
        PlacementFootprint swap = a;
        a = b;
        b = swap;
    }
    dx = dx > a.x ? dx - a.x : 0;
    dy = dy > a.y ? dy - a.y : 0;
    return (int64_t)dx * dx + (int64_t)dy * dy < (int64_t)b.x * b.x;
}

int can_place_monkey(unsigned type, int x, int y, int ignore)
{
    if (type >= MONKEY_COUNT || !tower_defs[type] ||
        (game.tower_count >= TOWER_LIMIT && ignore < 0))
        return 0;
    const TowerProfile *p = ignore >= 0 && ignore < TOWER_LIMIT && game.towers[ignore].active
                                ? tower_profile(&game.towers[ignore])
                            : tower_defs[type]->profile_count ? &tower_defs[type]->profiles[0]
                                                              : NULL;

    if (!p)
        return 0;
    PlacementFootprint shape = placement_footprint(p);
    int32_t wx = world_x(x), wy = world_y(y);
    int left = screen_x(wx - shape.x), right = screen_x(wx + shape.x);
    int top = screen_y(wy - shape.y), bottom = screen_y(wy + shape.y);

    if (left < 0 || top < 0 || right >= MAP_W || bottom >= MAP_H)
        return 0;

    for (int row = top; row <= bottom; row++)
        for (int col = left; col <= right; col++) {
            if (!shape.rectangle) {
                int32_t dx = world_x(col) - wx, dy = world_y(row) - wy;
                if (dx < 0)
                    dx = -dx;
                if (dy < 0)
                    dy = -dy;
                // Include the outer half-pixel touched by the circular footprint.
                dx = dx > (Q * 25 + 53) / 54 ? dx - (Q * 25 + 53) / 54 : 0;
                dy = dy > (Q * 15 + 27) / 28 ? dy - (Q * 15 + 27) / 28 : 0;
                if ((int64_t)dx * dx + (int64_t)dy * dy > (int64_t)shape.x * shape.x)
                    continue;
            }
            if (!(meadow_placement[row * 48 + col / 8] & (0x80 >> (col % 8))))
                return 0;
        }

    for (unsigned id = 0; id < TOWER_LIMIT; id++) {
        const Tower *t = &game.towers[id];
        if (!t->active || id == (unsigned)ignore)
            continue;
        PlacementFootprint other = placement_footprint(tower_profile(t));
        if (placement_overlaps(shape, wx, wy, other, t->x, t->y))
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
}

void game_start_round(void)
{
    if (game.running || game.won || game.lost || game.round >= 60)
        return;
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
    rounds_tick();
    towers_tick();
    support_units_tick();
    shots_tick();
    bloons_tick();
}
