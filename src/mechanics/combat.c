#include "game.h"
#include "animations.h"
#include <stdlib.h>
#include <string.h>

// Q14 sine values keep projectile rotation in integer arithmetic.
static const int16_t sine[360] = {
    0,      286,    572,    857,    1143,   1428,   1713,   1997,   2280,   2563,   2845,   3126,
    3406,   3686,   3964,   4240,   4516,   4790,   5063,   5334,   5604,   5872,   6138,   6402,
    6664,   6924,   7182,   7438,   7692,   7943,   8192,   8438,   8682,   8923,   9162,   9397,
    9630,   9860,   10087,  10311,  10531,  10749,  10963,  11174,  11381,  11585,  11786,  11982,
    12176,  12365,  12551,  12733,  12911,  13085,  13255,  13421,  13583,  13741,  13894,  14044,
    14189,  14330,  14466,  14598,  14726,  14849,  14968,  15082,  15191,  15296,  15396,  15491,
    15582,  15668,  15749,  15826,  15897,  15964,  16026,  16083,  16135,  16182,  16225,  16262,
    16294,  16322,  16344,  16362,  16374,  16382,  16384,  16382,  16374,  16362,  16344,  16322,
    16294,  16262,  16225,  16182,  16135,  16083,  16026,  15964,  15897,  15826,  15749,  15668,
    15582,  15491,  15396,  15296,  15191,  15082,  14968,  14849,  14726,  14598,  14466,  14330,
    14189,  14044,  13894,  13741,  13583,  13421,  13255,  13085,  12911,  12733,  12551,  12365,
    12176,  11982,  11786,  11585,  11381,  11174,  10963,  10749,  10531,  10311,  10087,  9860,
    9630,   9397,   9162,   8923,   8682,   8438,   8192,   7943,   7692,   7438,   7182,   6924,
    6664,   6402,   6138,   5872,   5604,   5334,   5063,   4790,   4516,   4240,   3964,   3686,
    3406,   3126,   2845,   2563,   2280,   1997,   1713,   1428,   1143,   857,    572,    286,
    0,      -286,   -572,   -857,   -1143,  -1428,  -1713,  -1997,  -2280,  -2563,  -2845,  -3126,
    -3406,  -3686,  -3964,  -4240,  -4516,  -4790,  -5063,  -5334,  -5604,  -5872,  -6138,  -6402,
    -6664,  -6924,  -7182,  -7438,  -7692,  -7943,  -8192,  -8438,  -8682,  -8923,  -9162,  -9397,
    -9630,  -9860,  -10087, -10311, -10531, -10749, -10963, -11174, -11381, -11585, -11786, -11982,
    -12176, -12365, -12551, -12733, -12911, -13085, -13255, -13421, -13583, -13741, -13894, -14044,
    -14189, -14330, -14466, -14598, -14726, -14849, -14968, -15082, -15191, -15296, -15396, -15491,
    -15582, -15668, -15749, -15826, -15897, -15964, -16026, -16083, -16135, -16182, -16225, -16262,
    -16294, -16322, -16344, -16362, -16374, -16382, -16384, -16382, -16374, -16362, -16344, -16322,
    -16294, -16262, -16225, -16182, -16135, -16083, -16026, -15964, -15897, -15826, -15749, -15668,
    -15582, -15491, -15396, -15296, -15191, -15082, -14968, -14849, -14726, -14598, -14466, -14330,
    -14189, -14044, -13894, -13741, -13583, -13421, -13255, -13085, -12911, -12733, -12551, -12365,
    -12176, -11982, -11786, -11585, -11381, -11174, -10963, -10749, -10531, -10311, -10087, -9860,
    -9630,  -9397,  -9162,  -8923,  -8682,  -8438,  -8192,  -7943,  -7692,  -7438,  -7182,  -6924,
    -6664,  -6402,  -6138,  -5872,  -5604,  -5334,  -5063,  -4790,  -4516,  -4240,  -3964,  -3686,
    -3406,  -3126,  -2845,  -2563,  -2280,  -1997,  -1713,  -1428,  -1143,  -857,   -572,   -286};
int fixed_sine(int degrees)
{
    return sine[(degrees % 360 + 360) % 360];
}

enum { CELLS_X = 20, CELLS_Y = 16, CELL = 16 * Q };
static int16_t heads[CELLS_X * CELLS_Y], next[BLOON_LIMIT];
static uint16_t active_ids[BLOON_LIMIT], outside_ids[BLOON_LIMIT];
static unsigned active_count, outside_count;
static unsigned indexed_radius;
static uint32_t indexed_time;
static unsigned indexed_bloons, index_valid;
static Shot *impact;
static uint32_t shots_epoch;
typedef struct {
    uint32_t token, fraction;
} CollisionHit;
static CollisionHit collision_hits[BLOON_LIMIT];
static uint32_t area_tokens[BLOON_LIMIT];
static unsigned area_tokens_in_use;
static int later_hit(CollisionHit a, CollisionHit b)
{
    return a.fraction > b.fraction ||
           (a.fraction == b.fraction && (a.token & 65535) > (b.token & 65535));
}

static void sift_hits(unsigned root, unsigned count)
{
    for (unsigned child = root * 2 + 1; child < count; child = root * 2 + 1) {
        if (child + 1 < count && later_hit(collision_hits[child + 1], collision_hits[child]))
            child++;
        if (!later_hit(collision_hits[child], collision_hits[root]))
            break;
        CollisionHit swap = collision_hits[root];
        collision_hits[root] = collision_hits[child];
        collision_hits[child] = swap;
        root = child;
    }
}

static void sort_hits(unsigned count)
{
    // Sort contacts along the segment before a hit can reuse a bloon slot.
    for (unsigned n = count / 2; n; n--)
        sift_hits(n - 1, count);
    for (unsigned n = count; n > 1; n--) {
        CollisionHit swap = collision_hits[0];
        collision_hits[0] = collision_hits[n - 1];
        collision_hits[n - 1] = swap;
        sift_hits(0, n - 1);
    }
}

static int cell_x(int32_t x)
{
    return (x + 160 * Q) / CELL;
}

static int cell_y(int32_t y)
{
    return (y + 128 * Q) / CELL;
}

static int clamp(int n, int lo, int hi)
{
    return n < lo ? lo : n > hi ? hi : n;
}

static uint16_t tags(const Bloon *b)
{
    uint16_t n = (b->flags & CAMO ? TAG_CAMO : 0) | (b->flags & FORTIFIED ? TAG_FORTIFIED : 0);
    if (b->type >= MOAB)
        n |= TAG_MOABS | (b->type == MOAB ? TAG_MOAB : TAG_BFB);
    if (b->type == CERAMIC)
        n |= TAG_CERAMIC;

    if (b->type == LEAD)
        n |= TAG_LEAD;

    if (b->type == BLACK)
        n |= TAG_BLACK;

    if (b->type == WHITE)
        n |= TAG_WHITE;

    if (b->type == ZEBRA)
        n |= TAG_ZEBRA;
    return n;
}

static int matches(const AttackEffect *e, const Bloon *b)
{
    uint16_t n = tags(b);
    if (n & e->exclude_tags)
        return 0;
    if (!e->tags)
        return 1;
    int match = (e->modifier_flags & 8) ? (n & e->tags) == e->tags : (n & e->tags) != 0;
    return (e->modifier_flags & 4) ? !match : match;
}

static void index_bloons(void)
{
    memset(heads, 255, sizeof heads);
    active_count = outside_count = 0;
    indexed_radius = 0;
    for (int i = bloon_next(0); i >= 0; i = bloon_next(i + 1)) {
        active_ids[active_count++] = i;
        Bloon *b = &game.bloons[i];
        if (bloon_defs[b->type].radius > indexed_radius)
            indexed_radius = bloon_defs[b->type].radius;
        int x = cell_x(b->x), y = cell_y(b->y);
        if (b->x < -160 * Q || b->y < -128 * Q || x < 0 || x >= CELLS_X || y < 0 ||
            y >= CELLS_Y) {
            next[i] = -1;
            outside_ids[outside_count++] = i;
            continue;
        }
        int cell = y * CELLS_X + x;
        next[i] = heads[cell];
        heads[cell] = i;
    }
    indexed_time = game.time;
    indexed_bloons = game.bloon_count;
    index_valid = 1;
}

void combat_reset_cache(void)
{
    index_valid = 0;
    active_count = outside_count = 0;
}

static int nearby(const Tower *a, const Tower *b, unsigned radius)
{
    return distance_squared(a->x, a->y, b->x, b->y) <=
           (int32_t)(radius / 16) * (int32_t)(radius / 16);
}

static int detects(const Tower *t, const AttackDef *a)
{
    if (a->camo)
        return 1;
    if (game.running)
        return t->support_camo;

    for (unsigned i = 0; i < TOWER_LIMIT; i++) {
        Tower *v = &game.towers[i];
        const TowerProfile *p = tower_profile(v);
        if (v->active && p && (p->support & S_VILLAGE) && p->tiers[1] >= 2 &&
            nearby(t, v, p->range))
            return 1;
    }
    return 0;
}

static int eligible(const Tower *t, const AttackDef *a, const Bloon *b)
{
    if (!b->active || ((b->flags & CAMO) && !(a->camo || (impact ? impact->camo : detects(t, a)))))
        return 0;
    if ((a->flags & A_MOAB_ONLY) && b->type < MOAB)
        return 0;

    if ((a->flags & A_NORMAL_ONLY) && b->type >= MOAB)
        return 0;

    if (a->collide_tags && !(tags(b) & a->collide_tags))
        return 0;

    if ((a->target_flags & AT_NO_CONCOCTION) && b->pop_attack &&
        (b->pop_attack->flags & A_CONCOCTION))
        return 0;

    if ((a->target_flags & AT_NO_WIND) && b->wind_remaining)
        return 0;

    if ((a->target_flags & AT_NO_MOAB) && b->type >= MOAB)
        return 0;

    if ((a->target_flags & AT_NO_FROZEN) && b->freeze)
        return 0;

    if ((a->target_flags & AT_NO_LEAD) && b->type == LEAD)
        return 0;

    if ((a->target_flags & AT_NO_GLUE) && b->glue) {
        unsigned level = 0;
        for (unsigned i = 0; i < a->effect_count; i++)
            if (a->effects[i].kind == EF_GLUE_LEVEL)
                level = a->effects[i].value;
        if (level <= b->glue_level)
            return 0;
    }
    return 1;
}

int combat_eligible(unsigned owner, const AttackDef *a, unsigned enemy)
{
    return owner < TOWER_LIMIT && enemy < BLOON_LIMIT &&
           eligible(&game.towers[owner], a, &game.bloons[enemy]);
}

static void target_candidate(const Tower *t, const AttackDef *a, int32_t x, int32_t y, unsigned id,
                             int *selected, int32_t *best)
{
    Bloon *b = &game.bloons[id];
    if (!eligible(t, a, b) ||
        ((a->target_flags & AT_REQUIRE_CAMO_TARGET) && !(b->flags & CAMO)))
        return;
    int32_t d = distance_squared(x, y, b->x, b->y);

    if (!(a->target_flags & AT_GLOBAL) && a->range &&
        d > (int32_t)(a->range / 16) * (a->range / 16))
        return;
    int32_t score = b->distance;

    if (*selected < 0 || score > *best || (score == *best && id < (unsigned)*selected)) {
        *selected = id;
        *best = score;
    }
}

int tower_target(const Tower *t, const AttackDef *a, int32_t x, int32_t y)
{
    if (!index_valid || !game.running || indexed_time != game.time ||
        indexed_bloons != game.bloon_count)
        index_bloons();
    int selected = -1;
    int32_t best = 0;

    if ((a->target_flags & AT_GLOBAL) || !a->range) {
        for (unsigned n = 0; n < active_count; n++)
            target_candidate(t, a, x, y, active_ids[n], &selected, &best);
    } else {

        // Q4 rounding accepts up to 15 extra Q8 units at the range boundary.
        int32_t radius = a->range + 15;
        int x0 = clamp(cell_x(x - radius), 0, CELLS_X - 1);
        int x1 = clamp(cell_x(x + radius), 0, CELLS_X - 1);
        int y0 = clamp(cell_y(y - radius), 0, CELLS_Y - 1);
        int y1 = clamp(cell_y(y + radius), 0, CELLS_Y - 1);
        for (int cy = y0; cy <= y1; cy++)
            for (int cx = x0; cx <= x1; cx++)
                for (int id = heads[cy * CELLS_X + cx]; id >= 0; id = next[id])
                    target_candidate(t, a, x, y, id, &selected, &best);
        for (unsigned n = 0; n < outside_count; n++)
            target_candidate(t, a, x, y, outside_ids[n], &selected, &best);
    }
    return selected;
}

static int immunity(unsigned owner, const AttackDef *a)
{
    Tower *t = &game.towers[owner];
    if (game.running && t->support_immunity)
        return a->immunity & ~t->support_immunity;
    if (!game.running)
        for (unsigned i = 0; i < TOWER_LIMIT; i++) {
            Tower *v = &game.towers[i];
            const TowerProfile *p = tower_profile(v);
            if (v->active && p && (p->support & S_VILLAGE) && p->tiers[1] >= 3 &&
                nearby(t, v, p->range))
                return 0;
        }

    if (t->alchemist && (a->immunity & IMM_LEAD))
        return a->immunity & ~IMM_LEAD;
    return a->immunity;
}

static unsigned damage_value(const AttackDef *a, const Bloon *b, unsigned base)
{
    if (!attack_has_damage(a))
        return 0;
    unsigned damage = base;
    if (b->type == CERAMIC)
        damage += a->ceramic;

    if (b->type >= MOAB)
        damage += a->moab;

    if (b->flags & FORTIFIED)
        damage += a->fortified;

    if (b->flags & CAMO)
        damage += a->camo_damage;

    for (unsigned i = 0; i < a->effect_count; i++) {
        const AttackEffect *e = &a->effects[i];
        if (e->kind != EF_DAMAGE_MODIFIER || !matches(e, b))
            continue;
        int represented = (e->tags == TAG_MOABS && a->moab == e->damage) ||
                          (e->tags == TAG_CERAMIC && a->ceramic == e->damage) ||
                          (e->tags == TAG_FORTIFIED && a->fortified == e->damage) ||
                          (e->tags == TAG_CAMO && a->camo_damage == e->damage);
        if (e->multiplier)
            damage = damage * e->multiplier / 1000;
        if (!represented)
            damage += e->damage;
    }

    if (impact) {
        if (b->type == CERAMIC)
            damage += impact->ceramic_bonus;
        if (b->type >= MOAB)
            damage += impact->moab_bonus;
        if (b->type == LEAD && (b->flags & FORTIFIED))
            damage += impact->fortified_lead_bonus;
    }
    return damage;
}

static void hit_effects(unsigned id, const AttackDef *a, unsigned owner)
{
    unsigned mask = impact ? a->immunity & ~impact->immunity_removed : immunity(owner, a);
    bloon_effect_buffed(id, a, owner, mask, impact);
    combat_effect(id, a, owner);
}

static int new_shot(unsigned owner, const AttackDef *a, int32_t x, int32_t y)
{
    int id = shot_acquire();
    if (id < 0)
        return -1;
    Shot *s = &game.shots[id];
    s->attack = a;
    s->owner = owner;
    s->x = x;
    s->y = y;
    s->born_tick = shots_epoch;
    s->origin_x = x;
    s->origin_y = y;
    s->life = (a->flags & A_TRAP) ? UINT32_MAX : (a->life ? a->life : 6000);
    s->pierce = a->pierce ? a->pierce : 1;
    s->damage = a->damage;
    s->target = UINT16_MAX;

    if (!impact)
        support_shot(owner, a, s);

    if (impact) {
        s->damage = attack_has_damage(a) ? a->damage + impact->damage_bonus : 0;
        s->pierce = a == impact->attack
                        ? impact->pierce
                        : (a->pierce + impact->brew_pierce) * impact->pierce_factor / 1000;
        s->pierce_factor = impact->pierce_factor;
        s->brew_pierce = impact->brew_pierce;
        s->damage_bonus = impact->damage_bonus;
        s->pierce_bonus = impact->pierce_bonus;
        s->immunity_removed = impact->immunity_removed;
        s->immunity = a->immunity & ~impact->immunity_removed;
        s->camo = a->camo || impact->camo;
        s->ceramic_bonus = impact->ceramic_bonus;
        s->moab_bonus = impact->moab_bonus;
        s->fortified_lead_bonus = impact->fortified_lead_bonus;
    }
    animation_shot(id);
    return id;
}

static int32_t normalised(int32_t value, int32_t scale, unsigned divisor)
{
    int64_t product = (int64_t)value * scale;
    if (product >= INT32_MIN && product <= INT32_MAX && divisor <= INT32_MAX)
        return (int32_t)product / (int32_t)divisor;
    return product / divisor;
}

static void aim(Shot *s, int32_t x, int32_t y)
{
    int32_t dx = x - s->x, dy = y - s->y;
    unsigned d = integer_sqrt(distance_squared(x, y, s->x, s->y)) * 16;
    if (!d)
        d = 1;
    s->dir_x = normalised(dx, 16384, d);
    s->dir_y = normalised(dy, 16384, d);
    if (s->attack->flags & A_RADIAL) {
        s->vx = s->attack->speed;
        s->vy = 0;
    } else {
        s->vx = normalised(dx, s->attack->speed, d);
        s->vy = normalised(dy, s->attack->speed, d);
    }
}

static void rotate(Shot *s, int degrees)
{
    degrees = (degrees % 360 + 360) % 360;
    int32_t si = sine[degrees], co = sine[(degrees + 90) % 360];
    int32_t x = s->vx, y = s->vy;
    s->vx = (int32_t)(((int64_t)x * co - (int64_t)y * si) / 16384);
    s->vy = (int32_t)(((int64_t)x * si + (int64_t)y * co) / 16384);
}

static void area_hit(unsigned owner, const AttackDef *a, int32_t x, int32_t y, unsigned damage)
{
    animation_area(owner, a, x, y);
    unsigned left = a->pierce;
    if (impact)
        left = a == impact->attack
                   ? impact->pierce
                   : (a->pierce + impact->brew_pierce) * impact->pierce_factor / 1000;

    if (!left)
        left = 65535;

    // Nested blasts need independent snapshots without exhausting the 16 KiB SH stack.
    uint32_t local_ids[32];
    uint32_t *ids = local_ids;
    int heap_ids = 0, shared_ids = 0;

    if (game.bloon_count > 32) {
        if (!area_tokens_in_use) {
            ids = area_tokens;
            area_tokens_in_use = shared_ids = 1;
        } else {
            ids = malloc(game.bloon_count * sizeof *ids);
            if (!ids) {
                game.pool_full = 1;
                return;
            }
            heap_ids = 1;
        }
    }

    unsigned count = 0;

    for (int i = bloon_next(0); i >= 0; i = bloon_next(i + 1)) {
        Bloon *b = &game.bloons[i];
        if (!eligible(&game.towers[owner], a, b))
            continue;
        if (collision_area_contains(a, x, y, b))
            ids[count++] = (b->generation << 16) | i;
    }

    for (unsigned j = 0; j < count && left; j++, left--) {
        unsigned i = ids[j] & 65535;
        Bloon *b = &game.bloons[i];
        if (!b->active || b->generation != ids[j] >> 16) {
            left++;
            continue;
        }
        unsigned n = damage_value(a, b, damage + (impact ? impact->damage_bonus : 0));
        hit_effects(i, a, owner);
        bloon_damage(i, n, impact ? (a->immunity & ~impact->immunity_removed) : immunity(owner, a),
                     owner);
    }

    if (shared_ids)
        area_tokens_in_use = 0;
    if (heap_ids)
        free(ids);
}

static void children(unsigned owner, const AttackDef *a, unsigned trigger, int32_t x, int32_t y,
                     int target)
{
    for (unsigned i = 0; i < a->child_count; i++) {
        const AttackDef *child = &a->children[i];
        if (child->trigger == trigger && (!impact || projectile_child_ready(impact, child)))
            attack_emit(owner, child, x, y, target);
    }
}

static void hit(unsigned id, unsigned enemy)
{
    Shot *s = &game.shots[id];
    const AttackDef *a = s->attack;
    Bloon *b = &game.bloons[enemy];
    if (!s->active || shot_history_contains(s, enemy))
        return;
    int allowed = projectile_can_hit(s, enemy);

    if (allowed <= 0) {
        if (allowed < 0)
            shot_release(s);
        return;
    }
    unsigned pierce_cost = projectile_pierce_cost(a, enemy);

    if (!shot_history_record(s, enemy)) {
        shot_release(s);
        return;
    }

    if (a->flags & A_EXPLOSION) {
        int32_t x = b->x, y = b->y;
        if (s->damage) {
            unsigned damage = damage_value(a, b, s->damage);
            hit_effects(enemy, a, s->owner);
            bloon_damage(enemy, damage, s->immunity, s->owner);
        }
        if (a->child_count)
            children(s->owner, a, TR_CONTACT, x, y, enemy);
        else
            area_hit(s->owner, a, x, y, attack_has_damage(a) ? s->damage - s->damage_bonus : 0);
        shot_release(s);
        return;
    }

    if (a->flags & A_TRAP) {
        combat_effect(enemy, a, s->owner);
        trap_capture(s, enemy);
        return;
    }
    children(s->owner, a, TR_CONTACT, b->x, b->y, enemy);
    unsigned damage = damage_value(a, b, s->damage);
    hit_effects(enemy, a, s->owner);
    children(s->owner, a, TR_DAMAGE, b->x, b->y, enemy);
    bloon_damage(enemy, damage, s->immunity, s->owner);
    s->pierce = s->pierce > pierce_cost ? s->pierce - pierce_cost : 0;
    children(s->owner, a, TR_PIERCE, s->x, s->y, enemy);

    if (!s->pierce)
        children(s->owner, a, TR_EXHAUST, s->x, s->y, enemy);

    if (!s->pierce && !projectile_keep_on_zero(a))
        shot_release(s);
}

void shot_hit(unsigned id, unsigned enemy)
{
    Shot *previous = impact;
    impact = &game.shots[id];
    hit(id, enemy);
    impact = previous;
}

static void emit(unsigned owner, const AttackDef *a, int32_t x, int32_t y, int target)
{
    if ((a->flags & A_TRAP) && !trap_emit_allowed(owner, a))
        return;
    if (a->flags & A_BEAM) {
        Tower *t = &game.towers[owner];
        animation_line(FX_BEAM, x, y, t->aim_x, t->aim_y);
        unsigned left = impact->pierce, count = 0;
        Shot ray = {.x = x, .y = y};
        for (int i = bloon_next(0); i >= 0; i = bloon_next(i + 1)) {
            Bloon *b = &game.bloons[i];
            if (!eligible(t, a, b))
                continue;
            int32_t fraction = collision_contact_fraction(&ray, t->aim_x, t->aim_y, b,
                                                          a->radius + bloon_defs[b->type].radius);
            if (fraction >= 0)
                collision_hits[count++] =
                    (CollisionHit){((uint32_t)b->generation << 16) | i, (unsigned)fraction};
        }
        sort_hits(count);
        for (unsigned n = 0; n < count && left; n++) {
            CollisionHit contact = collision_hits[n];
            unsigned i = contact.token & 65535;
            Bloon *b = &game.bloons[i];
            if (!b->active || b->generation != contact.token >> 16)
                continue;
            hit_effects(i, a, owner);
            bloon_damage(i, damage_value(a, b, a->damage + impact->damage_bonus),
                         a->immunity & ~impact->immunity_removed, owner);
            left--;
        }
        children(owner, a, TR_BEAM_END, t->aim_x, t->aim_y, target);
        return;
    }

    if (((a->flags & A_AREA) || ((a->flags & A_MORTAR) && a->trigger != TR_MAIN)) &&
        !a->curve_count && !a->speed && !a->refresh_pierce) {
        area_hit(owner, a, x, y, a->damage);
        return;
    }

    if ((a->flags & A_HITSCAN) && !(a->flags & A_CHAIN)) {
        if (target < 0)
            return;
        animation_line(FX_SPARK, game.bloons[target].x, game.bloons[target].y,
                       game.bloons[target].x, game.bloons[target].y);
        unsigned damage = damage_value(a, &game.bloons[target], a->damage + impact->damage_bonus);
        hit_effects(target, a, owner);
        children(owner, a, TR_CONTACT, game.bloons[target].x, game.bloons[target].y, target);
        children(owner, a, TR_DAMAGE, game.bloons[target].x, game.bloons[target].y, target);
        bloon_damage(target, damage, a->immunity & ~impact->immunity_removed, owner);
        return;
    }
    unsigned count = a->count ? a->count : 1;
    Shot direction;
    direction.x = x;
    direction.y = y;
    direction.attack = a;
    int32_t ax = target < 0 ? game.towers[owner].aim_x : game.bloons[target].x;
    int32_t ay = target < 0 ? game.towers[owner].aim_y : game.bloons[target].y;
    aim(&direction, ax, ay);
    int shared_setup = a->target_kind != 3 && !a->fixed_target && a->curve_kind != 2 &&
                       !a->random_spread &&
                       !(a->trigger == TR_MAIN &&
                         (a->flags & (A_SPIKE | A_WALL | A_FOAM | A_TRAP | A_MORTAR)));

    for (unsigned n = 0; n < count; n++) {
        int id = new_shot(owner, a, x, y);
        if (id < 0)
            break;
        Shot *s = &game.shots[id];
        s->target = target < 0 ? UINT16_MAX : target;
        s->dir_x = direction.dir_x;
        s->dir_y = direction.dir_y;
        s->vx = direction.vx;
        s->vy = direction.vy;
        if (a->flags & A_RADIAL) {
            s->vx = a->speed;
            s->vy = 0;
            rotate(s, n * 360 / count);
        } else if (count > 1)
            rotate(s, ((int)n * 2 - (int)count + 1) * a->spread / 2);
        if (shared_setup) {
            // Ordinary volleys share both the direction and deterministic destination.
            s->destination_x = ax;
            s->destination_y = ay;
            s->curve_time = a->curve_time;
        } else if (!projectile_setup(s, a, owner, target)) {
            shot_release(s);
            continue;
        }
        projectile_emission(s, a, n);
        const TowerProfile *p = tower_profile(&game.towers[owner]);
        for (unsigned k = 0; p && k < p->attack_count; k++)
            if (a == &p->attacks[k] && a->crit_every &&
                game.towers[owner].shots[k] % a->crit_every == 0)
                s->damage = a->crit_damage + s->damage_bonus;
    }
}

void attack_emit(unsigned owner, const AttackDef *a, int32_t x, int32_t y, int target)
{
    Shot context = {0};
    Shot *previous = impact;
    if (!impact) {
        context.attack = a;
        context.owner = owner;
        support_shot(owner, a, &context);
        impact = &context;
    }
    emit(owner, a, x, y, target);
    impact = previous;
}

void towers_tick(void)
{
    index_bloons();
    support_begin_tick();
    for (unsigned i = 0; i < TOWER_LIMIT; i++) {
        Tower *t = &game.towers[i];
        if (!t->active)
            continue;
        const TowerProfile *p = tower_profile(t);
        if (!p)
            continue;
        support_tick(i, p);
        if (p->support & S_AIR)
            air_tick(t, p);
        if (!game.running)
            continue;
        for (unsigned j = 0; j < p->attack_count; j++) {
            const AttackDef *a = &p->attacks[j];
            unsigned speed;
            AttackDef effective;
            support_attack(i, a, &effective, &speed);
            unsigned elapsed = TICK * speed / 1000;
            if (t->clocks[j] > elapsed) {
                t->clocks[j] -= elapsed;
                continue;
            }
            int32_t x = (p->support & S_AIR) ? t->air_x : t->x,
                    y = (p->support & S_AIR) ? t->air_y : t->y;
            int target = tower_target(t, &effective, x, y);
            if (target < 0 && !(a->target_flags & AT_FIRE_WITHOUT_TARGET) &&
                !(a->flags & (A_SPIKE | A_FOAM | A_TRAP)))
                continue;
            if ((a->flags & A_TRAP) && !trap_emit_allowed(i, a))
                continue;
            if (target >= 0) {
                t->aim_x = game.bloons[target].x;
                t->aim_y = game.bloons[target].y;
            }

            animation_fired(i, x, y, target);
            t->shots[j]++;
            attack_emit(i, a, x, y, target);
            unsigned overdue = elapsed - t->clocks[j];
            t->clocks[j] = (a->period ? a->period : 120) > overdue
                               ? (a->period ? a->period : 120) - overdue
                               : 0;
            support_fired(i);
        }
        if (t->type == 11)
            necromancy_tick(i, p);
    }
}

void shots_tick(void)
{
    index_bloons();
    shots_epoch++;
    Shot *previous = impact;
    for (unsigned cursor = 0; cursor < game.shot_count;) {
        unsigned i = game.shot_active[cursor];
        Shot *s = &game.shots[i];
        if (s->born_tick == shots_epoch) {
            cursor++;
            continue;
        }
        impact = s;
        const AttackDef *a = s->attack;
        if ((a->motion_flags & 512) && !game.towers[s->owner].active) {
            shot_release(s);
            goto next_shot;
        }
        if (!(a->motion_flags & 1024) || game.running)
            s->age += TICK;
        if (a->flags & A_TRAP) {
            trap_tick(s);
            if (!s->active)
                goto next_shot;
        }
        if (a->hit_reset && s->age - s->hit_clock >= a->hit_reset) {
            shot_history_reset(s);
            s->hit_clock = s->age;
        }
        children(s->owner, a, TR_INTERVAL, s->x, s->y, s->target == UINT16_MAX ? -1 : s->target);
        if (lightning_step(i))
            goto next_shot;
        if ((a->flags & A_CHAIN) && ((a->flags & A_HITSCAN) || s->history_count)) {
            if (s->chain_clock > TICK) {
                s->chain_clock -= TICK;
                goto next_shot;
            }
            s->chain_clock += a->chain_delay;
            int target = s->target == UINT16_MAX ? -1 : s->target;
            if (target >= 0 && (!game.bloons[target].active || shot_history_contains(s, target)))
                target = -1;
            if (target < 0) {
                int32_t best = INT32_MAX;
                for (int b = bloon_next(0); b >= 0; b = bloon_next(b + 1))
                    if (eligible(&game.towers[s->owner], a, &game.bloons[b]) &&
                        !shot_history_contains(s, b)) {
                        int32_t d =
                            distance_squared(s->x, s->y, game.bloons[b].x, game.bloons[b].y);
                        if (d < best &&
                            d <= (int32_t)(a->chain_range / 16) * (a->chain_range / 16)) {
                            target = b;
                            best = d;
                        }
                    }
            }
            if (target >= 0) {
                animation_line(FX_SPARK, game.bloons[target].x, game.bloons[target].y,
                               game.bloons[target].x, game.bloons[target].y);
                s->x = game.bloons[target].x;
                s->y = game.bloons[target].y;
                shot_hit(i, target);
                s->target = UINT16_MAX;
            } else
                shot_release(s);
            goto next_shot;
        }
        if (a->flags & A_MORTAR) {
            if (s->age >= s->life) {
                if (a->child_count) {
                    children(s->owner, a, TR_EXHAUST, s->x, s->y, -1);
                    children(s->owner, a, TR_CONTACT, s->x, s->y, -1);
                } else
                    area_hit(s->owner, a, s->x, s->y, s->damage - s->damage_bonus);
                shot_release(s);
            }
            goto next_shot;
        }
        int32_t nx, ny;
        int collision_ready = projectile_step(s, &nx, &ny);

        // Two truncated Q4 coordinates can expand a contact by up to 30 Q8 units.
        unsigned bloon_radius = indexed_radius + 31;
        if (bloon_radius > 30 * Q || (uint32_t)s->x + 200000u > 400000u ||
            (uint32_t)s->y + 200000u > 400000u || (uint32_t)nx + 200000u > 400000u ||
            (uint32_t)ny + 200000u > 400000u)
            bloon_radius = 30 * Q;
        int radius = a->radius + bloon_radius;
        int x0 = clamp(cell_x((s->x < nx ? s->x : nx) - radius), 0, CELLS_X - 1);
        int x1 = clamp(cell_x((s->x > nx ? s->x : nx) + radius), 0, CELLS_X - 1);
        int y0 = clamp(cell_y((s->y < ny ? s->y : ny) - radius), 0, CELLS_Y - 1);
        int y1 = clamp(cell_y((s->y > ny ? s->y : ny) + radius), 0, CELLS_Y - 1);
        if (!collision_ready)
            goto collision_done;
        if ((a->flags & A_TRAP) && (s->trap_close || s->trap_full))
            goto collision_done;
        unsigned count = 0;
        for (int cy = y0; cy <= y1; cy++)
            for (int cx = x0; cx <= x1; cx++)
                for (int b = heads[cy * CELLS_X + cx]; b >= 0; b = next[b]) {
                    Bloon *enemy = &game.bloons[b];
                    if (!eligible(&game.towers[s->owner], a, enemy) || shot_history_contains(s, b))
                        continue;
                    int32_t fraction = collision_contact_fraction(
                        s, nx, ny, enemy, a->radius + bloon_defs[enemy->type].radius);
                    if (fraction >= 0)
                        collision_hits[count++] = (CollisionHit){
                            ((uint32_t)enemy->generation << 16) | (unsigned)b, (unsigned)fraction};
                }

        sort_hits(count);
        int32_t sx = s->x, sy = s->y;
        for (unsigned n = 0; n < count && s->active; n++) {
            CollisionHit contact = collision_hits[n];
            unsigned b = contact.token & 65535;
            Bloon *enemy = &game.bloons[b];
            if (!enemy->active || enemy->generation != contact.token >> 16 ||
                !eligible(&game.towers[s->owner], a, enemy) || shot_history_contains(s, b))
                continue;
            s->x = sx + (int64_t)(nx - sx) * contact.fraction / 65536;
            s->y = sy + (int64_t)(ny - sy) * contact.fraction / 65536;
            int32_t hx = enemy->x, hy = enemy->y;
            shot_hit(i, b);
            if (a->flags & A_CHAIN) {
                nx = hx;
                ny = hy;
                s->chain_clock = a->chain_delay;
                break;
            }
        }
    collision_done:
        s->x = nx;
        s->y = ny;
        if (s->active && s->age >= s->life) {
            children(s->owner, a, TR_EXHAUST, s->x, s->y, -1);
            shot_release(s);
        }
    next_shot:

        // Releasing swaps the last active shot here; children wait one update.
        if (cursor < game.shot_count && game.shot_active[cursor] == i)
            cursor++;
    }
    impact = previous;
}

void combat_effect(unsigned id, const AttackDef *a, unsigned owner)
{
    if (id >= BLOON_LIMIT || !game.bloons[id].active)
        return;
    Bloon *b = &game.bloons[id];
    for (unsigned i = 0; i < a->effect_count; i++) {
        const AttackEffect *e = &a->effects[i];
        if (e->kind != EF_WIND && !matches(e, b))
            continue;
        if (e->flags & EF_ONLY_DAMAGED) {
            unsigned properties =
                (bloon_defs[b->type].immunity | (b->freeze ? IMM_FROZEN : 0)) & ~b->property_strip;
            unsigned mask = impact ? a->immunity & ~impact->immunity_removed : immunity(owner, a);
            unsigned base = impact && a == impact->attack
                                ? impact->damage
                                : a->damage + (impact ? impact->damage_bonus : 0);
            if ((properties & mask) || !damage_value(a, b, base))
                continue;
        }
        if (e->chance && e->chance < 1000 && game_random() % 1000 >= e->chance)
            continue;
        if ((e->flags & EF_PREVENT_MOAB) && b->type >= MOAB)
            continue;
        if (e->kind == EF_PUSH) {
            if (b->type >= MOAB && !e->auxiliary[0])
                continue;
            int32_t push = e->value;
            if (b->type < MOAB && e->auxiliary[0] > e->value)
                push += game_random() % (e->auxiliary[0] - e->value + 1);
            if (b->type >= MOAB)
                push = push * e->auxiliary[b->type == BFB ? 1 : 0] / 1000;
            b->distance -= push;
            if (b->distance < 0)
                b->distance = 0;
        }
        if (e->kind == EF_KNOCKBACK) {
            int heavy = b->type == LEAD || b->type == CERAMIC || (b->flags & FORTIFIED);
            unsigned multiplier = e->auxiliary[b->type >= MOAB ? 0 : heavy ? 1 : 2];
            if (multiplier) {
                b->knockback = e->time;
                b->knockback_multiplier = multiplier;
            }
        }
        if (e->kind == EF_WIND) {
            unsigned mask = impact ? a->immunity & ~impact->immunity_removed : a->immunity;
            projectile_wind_apply(b, e, mask);
        }
        if (e->kind == EF_REMOVE_GLUE_ICE)
            bloon_clear_glue_ice(b);
        if (e->kind == EF_CLEANSE) {
            if (e->modifier_flags & 32) {
                unsigned imm = bloon_defs[b->type].immunity | (b->freeze ? IMM_FROZEN : 0);
                unsigned mask = impact ? a->immunity & ~impact->immunity_removed : a->immunity;
                if ((imm & ~b->property_strip) & mask)
                    continue;
            }
            if (e->value & 1)
                b->flags &= ~CAMO;
            if (e->value & 2)
                b->flags &= ~REGROW;
            if ((e->value & 4) && (b->flags & FORTIFIED)) {
                b->flags &= ~FORTIFIED;
                unsigned extra = bloon_defs[b->type].fort_hp - bloon_defs[b->type].hp;
                b->hp = b->hp > extra ? b->hp - extra : 1;
            }
            if ((e->value & 8) && b->type == LEAD)
                bloon_damage(id, b->hp, 0, owner);
        }
        if (e->kind == EF_VULNERABLE) {
            b->vulnerability = e->time;
            b->vulnerable_damage = e->damage;
        }
        if (e->kind == EF_CONCOCTION || e->kind == EF_SHARDS) {
            b->pop_attack = a;
            b->pop_source = owner;
        }
    }

    for (unsigned i = 0; i < a->child_count; i++)
        if (a->children[i].trigger == TR_POP) {
            b->pop_attack = a;
            b->pop_source = owner;
        }
}

struct CombatPop {
    const Bloon *parent;
    AttackDef blast;
    Shot context;
    Shot *previous_impact;
    uint32_t local_ids[32], *ids;
    unsigned child, count, cursor, left;
    uint8_t area_active, pooled;
};

static CombatPop pop_frames[16];
static unsigned pop_depth;

CombatPop *combat_pop_begin(const Bloon *b)
{
    if (!b->pop_attack)
        return NULL;
    int immediate = 0;
    for (unsigned i = 0; i < b->pop_attack->child_count; i++) {
        const AttackDef *child = &b->pop_attack->children[i];
        if (child->trigger == TR_POP && child->source_damage_count)
            immediate = 1;
    }
    if (!immediate) {
        for (unsigned i = 0; i < b->pop_attack->child_count; i++) {
            const AttackDef *child = &b->pop_attack->children[i];
            if (child->trigger == TR_POP)
                attack_emit(b->pop_source, child, b->x, b->y, -1);
        }
        return NULL;
    }

    CombatPop *pop = pop_depth < 16 ? &pop_frames[pop_depth] : malloc(sizeof *pop);
    if (!pop) {
        game.pool_full = 1;
        return NULL;
    }
    pop->pooled = pop_depth < 16;
    pop_depth++;
    pop->parent = b;
    pop->child = 0;
    pop->area_active = 0;
    return pop;
}

static void pop_area_finish(CombatPop *pop)
{
    if (!pop->area_active)
        return;
    impact = pop->previous_impact;
    if (pop->ids != pop->local_ids)
        free(pop->ids);
    pop->area_active = 0;
}

static int pop_area_begin(CombatPop *pop, const AttackDef *child)
{
    const Bloon *b = pop->parent;
    pop->blast = *child;
    unsigned normal = child->default_bloon_damage, large = child->default_moab_damage;
    unsigned source = tags(b);
    for (unsigned i = 0; i < child->source_damage_count; i++) {
        const AttackDamageSource *damage = &child->source_damage[i];
        if (source & damage->source_tag) {
            normal = damage->bloon_damage;
            large = damage->moab_damage;
            break;
        }
    }
    pop->blast.damage = normal;
    pop->blast.moab = large - normal;
    memset(&pop->context, 0, sizeof pop->context);
    pop->context.attack = &pop->blast;
    support_shot(b->pop_source, &pop->blast, &pop->context);
    pop->previous_impact = impact;
    impact = &pop->context;
    pop->left = pop->context.pierce ? pop->context.pierce : 65535;
    pop->cursor = pop->count = 0;
    pop->ids = pop->local_ids;

    if (game.bloon_count > 32) {
        pop->ids = malloc(game.bloon_count * sizeof *pop->ids);
        if (!pop->ids) {
            game.pool_full = 1;
            impact = pop->previous_impact;
            return 0;
        }
    }
    animation_area(b->pop_source, child, b->x, b->y);
    pop->area_active = 1;
    for (int id = bloon_next(0); id >= 0; id = bloon_next(id + 1)) {
        Bloon *enemy = &game.bloons[id];
        if (eligible(&game.towers[b->pop_source], &pop->blast, enemy) &&
            collision_area_contains(&pop->blast, b->x, b->y, enemy))
            pop->ids[pop->count++] = ((uint32_t)enemy->generation << 16) | id;
    }
    return 1;
}

int combat_pop_next(CombatPop *pop, unsigned *enemy, unsigned *damage, unsigned *immunity)
{
    for (;;) {
        while (pop->area_active && pop->cursor < pop->count && pop->left) {
            uint32_t token = pop->ids[pop->cursor++];
            unsigned id = token & 65535;
            Bloon *b = &game.bloons[id];
            if (!b->active || b->generation != token >> 16)
                continue;
            *enemy = id;
            *damage = damage_value(&pop->blast, b, pop->blast.damage + pop->context.damage_bonus);
            hit_effects(id, &pop->blast, pop->parent->pop_source);
            *immunity = pop->blast.immunity & ~pop->context.immunity_removed;
            pop->left--;
            return 1;
        }
        pop_area_finish(pop);

        const AttackDef *attack = pop->parent->pop_attack;
        if (pop->child >= attack->child_count)
            return 0;
        const AttackDef *child = &attack->children[pop->child++];
        if (child->trigger != TR_POP)
            continue;
        if (child->source_damage_count)
            pop_area_begin(pop, child);
        else
            attack_emit(pop->parent->pop_source, child, pop->parent->x, pop->parent->y, -1);
    }
}

void combat_pop_finish(CombatPop *pop)
{
    if (!pop)
        return;
    pop_area_finish(pop);
    pop_depth--;
    if (!pop->pooled)
        free(pop);
}
