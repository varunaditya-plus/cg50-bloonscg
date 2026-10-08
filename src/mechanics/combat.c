#include "game.h"
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
static uint32_t indexed_time;
static unsigned indexed_bloons, index_valid;
static Shot *impact;
static uint32_t shots_epoch;
typedef struct {
    uint32_t token, fraction;
} CollisionHit;
static CollisionHit collision_hits[BLOON_LIMIT];
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
    for (int i = 0; i < BLOON_LIMIT; i++)
        if (game.bloons[i].active) {
            active_ids[active_count++] = i;
            Bloon *b = &game.bloons[i];
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
    if (!eligible(t, a, b))
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
    return id;
}

static void aim(Shot *s, int32_t x, int32_t y)
{
    int32_t dx = x - s->x, dy = y - s->y;
    unsigned d = integer_sqrt(distance_squared(x, y, s->x, s->y)) * 16;
    if (!d)
        d = 1;
    s->dir_x = (int32_t)((int64_t)dx * 16384 / d);
    s->dir_y = (int32_t)((int64_t)dy * 16384 / d);
    s->vx = (int32_t)((int64_t)dx * s->attack->speed / d);
    s->vy = (int32_t)((int64_t)dy * s->attack->speed / d);
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
    unsigned left = a->pierce;
    if (impact)
        left = a == impact->attack
                   ? impact->pierce
                   : (a->pierce + impact->brew_pierce) * impact->pierce_factor / 1000;

    if (!left)
        left = 65535;

    // Snapshot generations so newly spawned descendants are not hit again.
    uint32_t ids[BLOON_LIMIT];
    unsigned count = 0;

    for (unsigned i = 0; i < BLOON_LIMIT; i++) {
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
        unsigned left = impact->pierce, count = 0;
        Shot ray = {.x = x, .y = y};
        for (unsigned i = 0; i < BLOON_LIMIT; i++) {
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
        unsigned damage = damage_value(a, &game.bloons[target], a->damage + impact->damage_bonus);
        hit_effects(target, a, owner);
        children(owner, a, TR_CONTACT, game.bloons[target].x, game.bloons[target].y, target);
        children(owner, a, TR_DAMAGE, game.bloons[target].x, game.bloons[target].y, target);
        bloon_damage(target, damage, a->immunity & ~impact->immunity_removed, owner);
        return;
    }
    unsigned count = a->count ? a->count : 1;

    for (unsigned n = 0; n < count; n++) {
        int id = new_shot(owner, a, x, y);
        if (id < 0)
            break;
        Shot *s = &game.shots[id];
        s->target = target < 0 ? UINT16_MAX : target;
        int32_t ax = target < 0 ? game.towers[owner].aim_x : game.bloons[target].x;
        int32_t ay = target < 0 ? game.towers[owner].aim_y : game.bloons[target].y;
        aim(s, ax, ay);
        if (a->flags & A_RADIAL) {
            s->vx = a->speed;
            s->vy = 0;
            rotate(s, n * 360 / count);
        } else if (count > 1)
            rotate(s, ((int)n * 2 - (int)count + 1) * a->spread / 2);
        if (!projectile_setup(s, a, owner, target)) {
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

            t->shots[j]++;
            attack_emit(i, a, x, y, target);
            unsigned overdue = elapsed - t->clocks[j];
            t->clocks[j] = (a->period ? a->period : 120) > overdue
                               ? (a->period ? a->period : 120) - overdue
                               : 0;
            support_fired(i);
        }
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
                for (unsigned b = 0; b < BLOON_LIMIT; b++)
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

        int radius = a->radius + 30 * Q;
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

void combat_bloon_pop(const Bloon *b)
{
    if (!b->pop_attack)
        return;
    for (unsigned i = 0; i < b->pop_attack->child_count; i++) {
        const AttackDef *child = &b->pop_attack->children[i];
        if (child->trigger != TR_POP)
            continue;
        if (child->source_damage_count) {
            AttackDef blast = *child;
            unsigned normal = child->default_bloon_damage, large = child->default_moab_damage;
            unsigned source = tags(b);
            for (unsigned j = 0; j < child->source_damage_count; j++) {
                const AttackDamageSource *d = &child->source_damage[j];
                if (source & d->source_tag) {
                    normal = d->bloon_damage;
                    large = d->moab_damage;
                    break;
                }
            }
            blast.damage = normal;
            blast.moab = large - normal;
            Shot *previous = impact;
            Shot context = {0};
            context.attack = &blast;
            support_shot(b->pop_source, &blast, &context);
            impact = &context;
            area_hit(b->pop_source, &blast, b->x, b->y, normal);
            impact = previous;
        } else
            attack_emit(b->pop_source, child, b->x, b->y, -1);
    }
}
