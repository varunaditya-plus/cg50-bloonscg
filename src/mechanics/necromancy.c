#include "game.h"
#include "animations.h"

// https://raw.githubusercontent.com/KyleDerZweite/btd6-atlas/ded3155921d70cd83d803b4d70022000e4c37c6b/data/56.3-build-24829026/game-data/Towers/WizardMonkey/WizardMonkey-004.json
// https://store.steampowered.com/news/posts/?appids=960090&enddate=1602715731&feed=steam_community_announcements
// Native NecromancerZone.OnBloonDegrade: https://store.steampowered.com/app/960090/Bloons_TD_6/

static const AttackDef undead_bloon[] = {
    {
        .life = 42000,
        .speed = 6400,
        .range = 15360,
        .radius = 768,
        .damage = 2,
        .pierce = 1,
        .count = 1,
        .camo = 1,
        .motion_flags = MF_PATH_REVERSE | 64 | 2048,
    },
    {
        .life = 60000,
        .speed = 6400,
        .range = 15360,
        .radius = 768,
        .damage = 2,
        .pierce = 1,
        .count = 1,
        .camo = 1,
        .motion_flags = MF_PATH_REVERSE | 64 | 2048,
    },
};

static uint16_t necromancers[TOWER_LIMIT], track_ranges[TOWER_LIMIT];
static int32_t track_spawns[TOWER_LIMIT];
static unsigned necromancer_count, cache_valid;

void necromancy_reset_cache(void)
{
    cache_valid = 0;
}

static const TowerProfile *necromancer(const Tower *t)
{
    if (!t->active || t->type != 11 || t->profile < 18)
        return NULL;
    const TowerProfile *p = tower_profile(t);
    return p && p->tiers[2] == 4 ? p : NULL;
}

static void cache_necromancers(void)
{
    if (cache_valid)
        return;
    necromancer_count = 0;

    for (unsigned i = 0; i < TOWER_LIMIT; i++)
        if (necromancer(&game.towers[i])) {
            necromancers[necromancer_count++] = i;
            track_ranges[i] = 0;
            track_spawns[i] = -1;
        }
    cache_valid = 1;
}

static unsigned grave_total(const Tower *t)
{
    return t->necro_grave[0] + t->necro_grave[1];
}

void necromancy_begin_tick(void)
{
    cache_necromancers();

    for (unsigned i = 0; i < necromancer_count; i++) {
        Tower *t = &game.towers[necromancers[i]];
        if (t->necro_round == game.round)
            continue;

        // Only the current and previous round's souls survive; spend older souls first.
        t->necro_grave[1] = game.round - t->necro_round == 1 ? t->necro_grave[0] : 0;
        t->necro_grave[0] = 0;
        t->necro_round = game.round;
    }
}

unsigned necromancy_damage(unsigned owner)
{
    return owner < TOWER_LIMIT && necromancer(&game.towers[owner])
               ? grave_total(&game.towers[owner]) / 200
               : 0;
}

static int can_absorb(const Tower *t, const Bloon *b)
{
    if (grave_total(t) >= 500)
        return 0;
    unsigned range = 60 * Q * (t->support_range ? t->support_range : 1000) / 1000;
    if (t->buff_clock && t->buff_shots)
        range = range * (t->brew_range ? t->brew_range : 1000) / 1000;
    return distance_squared(t->x, t->y, b->x, b->y) <=
           (int32_t)(range / 16) * (int32_t)(range / 16);
}

unsigned necromancy_pop(const Bloon *b, unsigned owner, unsigned recipient)
{
    if (recipient == NECRO_NONE)
        return NECRO_NONE;
    cache_necromancers();
    if (!necromancer_count)
        return NECRO_NONE;

    // Descendant layers from one damage event retain the first chosen graveyard.
    if (recipient < TOWER_LIMIT) {
        Tower *t = &game.towers[recipient];
        if (grave_total(t) < 500)
            t->necro_grave[0]++;
        return recipient;
    }

    // The popping Necromancer absorbs first; other overlapping graves share each event once.
    if (owner < TOWER_LIMIT && necromancer(&game.towers[owner]) &&
        can_absorb(&game.towers[owner], b)) {
        game.towers[owner].necro_grave[0]++;
        return owner;
    }
    unsigned candidates = 0;

    for (unsigned i = 0; i < necromancer_count; i++)
        candidates += can_absorb(&game.towers[necromancers[i]], b);
    if (!candidates)
        return NECRO_NONE;
    unsigned chosen = candidates > 1 ? game_random() % candidates : 0;

    for (unsigned i = 0; i < necromancer_count; i++) {
        unsigned id = necromancers[i];
        Tower *t = &game.towers[id];
        if (!can_absorb(t, b))
            continue;
        if (!chosen) {
            t->necro_grave[0]++;
            return id;
        }
        chosen--;
    }
    return NECRO_NONE;
}

static int32_t clamp(int32_t n, int32_t lo, int32_t hi)
{
    return n < lo ? lo : n > hi ? hi : n;
}

static int inside_segment(const PathPoint *p, const PathPoint *next, int32_t x, int32_t y,
                          unsigned range, unsigned fraction)
{
    int32_t px = p->x + (int64_t)(next->x - p->x) * fraction / 65536;
    int32_t py = p->y + (int64_t)(next->y - p->y) * fraction / 65536;
    return distance_squared(x, y, px, py) <= (int32_t)(range / 16) * (int32_t)(range / 16);
}

static int32_t track_spawn(const Tower *t, unsigned range)
{
    int32_t furthest = -1;

    for (unsigned i = 0; i + 1 < meadow_path_count; i++) {
        const PathPoint *p = &meadow_path[i], *next = p + 1;
        int32_t dx = next->x - p->x, dy = next->y - p->y;
        int64_t squared = (int64_t)dx * dx + (int64_t)dy * dy;
        if (!squared)
            continue;
        unsigned closest = clamp(((int64_t)(t->x - p->x) * dx +
                                  (int64_t)(t->y - p->y) * dy) * 65536 / squared, 0, 65536);
        if (!inside_segment(p, next, t->x, t->y, range, closest))
            continue;
        unsigned low = closest, high = 65536;

        if (inside_segment(p, next, t->x, t->y, range, high))
            low = high;
        else
            while (high - low > 1) {
                unsigned middle = (high + low) / 2;
                if (inside_segment(p, next, t->x, t->y, range, middle))
                    low = middle;
                else
                    high = middle;
            }
        furthest = p->distance + (uint64_t)(next->distance - p->distance) * low / 65536;
    }
    return furthest;
}

static unsigned track_index(unsigned distance)
{
    unsigned low = 0, high = meadow_path_count - 1;

    while (high - low > 1) {
        unsigned middle = (high + low) / 2;
        if (meadow_path[middle].distance <= distance)
            low = middle;
        else
            high = middle;
    }
    return low;
}

static void undead_position(const Shot *s, int32_t distance, int32_t *x, int32_t *y)
{
    const PathPoint *p = &meadow_path[s->dir_x], *next = p + 1;
    int32_t dx = next->x - p->x, dy = next->y - p->y;
    unsigned length = next->distance - p->distance;
    unsigned along = distance - p->distance;
    int32_t offset = s->destination_x;
    // Meadow segments keep both interpolation numerators within int32_t.
    *x = p->x + (dx * (int32_t)along - dy * offset) / (int32_t)length;
    *y = p->y + (dy * (int32_t)along + dx * offset) / (int32_t)length;
}

static void spend_grave(Tower *t, unsigned amount)
{
    unsigned old = amount < t->necro_grave[1] ? amount : t->necro_grave[1];
    t->necro_grave[1] -= old;
    t->necro_grave[0] -= amount - old;
    t->necro_used += amount;
}

void necromancy_tick(unsigned owner, const TowerProfile *p)
{
    Tower *t = &game.towers[owner];
    if (!game.running || t->type != 11 || p->tiers[2] != 4)
        return;
    const AttackDef *a = &undead_bloon[p->tiers[0] > 0];
    AttackDef effective;
    unsigned speed;
    support_attack(owner, a, &effective, &speed);
    unsigned elapsed = TICK * speed / 1000;

    if (t->necro_clock > elapsed) {
        t->necro_clock -= elapsed;
        return;
    }
    unsigned grave = grave_total(t);
    if (!grave)
        return;

    if (game.time - t->necro_budget_clock >= 6000) {
        t->necro_budget_clock = game.time;
        t->necro_used = 0;
    }
    if (t->necro_used >= 50 || !game.shot_free_count)
        return;

    if (track_ranges[owner] != effective.range) {
        track_ranges[owner] = effective.range;
        track_spawns[owner] = track_spawn(t, effective.range);
    }
    if (track_spawns[owner] < 0)
        return;

    int target = tower_target(t, &effective, t->x, t->y);
    unsigned minimum = target >= 0 ? game.bloons[target].distance : 0;
    unsigned maximum = track_spawns[owner];
    if (minimum > maximum)
        minimum = maximum;
    unsigned period = 9000 * (1000 - 100 * (grave / 100)) / 1000;
    unsigned count = 1 + game_random() % 5;

    for (unsigned i = 0; i < count && grave_total(t) && t->necro_used < 50; i++) {
        if (!game.shot_free_count)
            break;
        int id = shot_acquire();
        Shot *s = &game.shots[id];
        s->owner = owner;
        s->attack = a;
        s->life = a->life;
        s->target = UINT16_MAX;
        support_shot(owner, a, s);
        unsigned available = grave_total(t);
        if (available > 50u - t->necro_used)
            available = 50u - t->necro_used;
        if (available > 10)
            available = 10;
        unsigned spent = 1 + game_random() % available;
        s->pierce = (spent + 1 + s->brew_pierce) * s->pierce_factor / 1000;
        unsigned spread = maximum - minimum;
        if (spread > 50 * Q)
            spread = 50 * Q;
        s->curve_time = maximum - game_random() % (spread + 1);
        s->dir_x = track_index(s->curve_time);
        s->destination_x = (int32_t)(game_random() % (10 * Q + 1)) - 5 * Q;
        undead_position(s, s->curve_time, &s->x, &s->y);
        s->origin_x = s->x;
        s->origin_y = s->y;
        animation_shot(id);
        spend_grave(t, spent);
    }
    unsigned overdue = elapsed - t->necro_clock;
    t->necro_clock = period > overdue ? period - overdue : 0;
    support_fired(owner);
}

int necromancy_step(Shot *s, int32_t *nx, int32_t *ny)
{
    unsigned step = s->attack->speed / 50;
    s->curve_time = s->curve_time > step ? s->curve_time - step : 0;

    while (s->dir_x && meadow_path[s->dir_x].distance > s->curve_time)
        s->dir_x--;
    undead_position(s, s->curve_time, nx, ny);
    s->vx = (*nx - s->x) * 50;
    s->vy = (*ny - s->y) * 50;
    if (!s->curve_time)
        s->life = s->age;
    return s->pierce != 0;
}
