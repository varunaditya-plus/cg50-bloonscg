#include "game.h"
#include <string.h>

extern const uint8_t meadow_placement[];
static uint32_t timer(uint32_t n)
{
    return n > TICK ? n - TICK : 0;
}

static uint16_t bounded(unsigned n)
{
    return n > 65535 ? 65535 : n;
}

static int inside(int32_t x, int32_t y, int32_t a, int32_t b, unsigned radius)
{
    return distance_squared(x, y, a, b) <= (int32_t)(radius / 16) * (int32_t)(radius / 16);
}

static int nearby(const Tower *a, const Tower *b, unsigned radius)
{
    return inside(a->x, a->y, b->x, b->y, radius);
}

static unsigned own_range(const Tower *t, const TowerProfile *p)
{
    unsigned range = p->range;
    if (t->buff_clock && t->buff_shots)
        range = range * t->brew_range / 1000;
    return bounded(range);
}

void support_begin_tick(void)
{
    uint16_t active_ids[TOWER_LIMIT];
    unsigned active_count = 0;
    for (unsigned i = 0; i < TOWER_LIMIT; i++) {
        Tower *t = &game.towers[i];
        t->support_rate = t->support_range = t->support_pierce = 1000;
        t->support_immunity = t->support_camo = 0;
        if (t->active) {
            active_ids[active_count++] = i;
            t->buff_clock = timer(t->buff_clock);
            t->brew_block = timer(t->brew_block);
            if (!t->buff_clock || !t->buff_shots)
                t->alchemist = 0;
        }
    }
    // Resolve Village range chains before distributing unique, non-stacking auras.
    uint16_t queue[TOWER_LIMIT * 2];
    unsigned head = 0, tail = 0;

    for (unsigned n = 0; n < active_count; n++) {
        unsigned i = active_ids[n];
        Tower *t = &game.towers[i];
        const TowerProfile *p = tower_profile(t);
        if (p && (p->support & S_VILLAGE))
            queue[tail++] = i;
    }

    while (head < tail) {
        Tower *t = &game.towers[queue[head++]];
        const TowerProfile *p = tower_profile(t);
        unsigned radius = own_range(t, p) * t->support_range / 1000;
        unsigned factor = p->buff_range_multiplier ? p->buff_range_multiplier : 1000;
        for (unsigned n = 0; n < active_count; n++) {
            unsigned j = active_ids[n];
            Tower *v = &game.towers[j];
            const TowerProfile *vp = tower_profile(v);
            if (!vp || !(vp->support & S_VILLAGE) || v == t ||
                factor <= v->support_range || !nearby(t, v, radius))
                continue;
            v->support_range = factor;
            if (tail < TOWER_LIMIT * 2)
                queue[tail++] = j;
        }
    }

    for (unsigned n = 0; n < active_count; n++) {
        unsigned i = active_ids[n];
        Tower *t = &game.towers[i];
        const TowerProfile *p = tower_profile(t);
        if (!p || !(p->support & S_VILLAGE))
            continue;
        unsigned radius = own_range(t, p) * t->support_range / 1000;
        for (unsigned k = 0; k < active_count; k++) {
            unsigned j = active_ids[k];
            Tower *v = &game.towers[j];
            const TowerProfile *vp = tower_profile(v);
            if (i == j || !vp || !nearby(t, v, radius))
                continue;
            unsigned factor = p->buff_range_multiplier ? p->buff_range_multiplier : 1000;
            if (factor > v->support_range)
                v->support_range = factor;
            if (p->buff_speed && p->buff_speed < v->support_rate)
                v->support_rate = p->buff_speed;
            v->support_camo |= p->support_camo;
            v->support_immunity |= p->support_immunity;
        }
    }

    for (unsigned n = 0; n < active_count; n++) {
        unsigned j = active_ids[n];
        Tower *v = &game.towers[j];
        if (v->type != 13)
            continue;
        unsigned stacks = 0, maximum = 20;
        uint32_t rate = 1000000000u;
        for (unsigned k = 0; k < active_count && stacks < maximum; k++) {
            unsigned i = active_ids[k];
            Tower *t = &game.towers[i];
            const TowerProfile *p = tower_profile(t);
            if (i == j || !p || !(p->support & S_SHINOBI))
                continue;
            unsigned radius = own_range(t, p) * t->support_range / 1000;
            if (!nearby(t, v, radius))
                continue;
            maximum = p->support_max_stacks ? p->support_max_stacks : 20;
            rate = (uint64_t)rate * (p->support_stacks ? p->support_stacks : 920) / 1000;
            stacks++;
        }
        v->support_rate = ((uint64_t)v->support_rate * rate + 500000000u) / 1000000000u;
        v->support_pierce = 1000 + 80 * stacks;
    }
}

static unsigned pierce_factor(const Tower *t, const AttackDef *a)
{
    unsigned factor = t->support_pierce ? t->support_pierce : 1000;
    unsigned gained = t->lives_gained;
    if (gained > a->life_pierce_cap)
        gained = a->life_pierce_cap;
    return bounded(factor * (1000 + gained * a->life_pierce_per) / 1000);
}

void support_attack(unsigned owner, const AttackDef *raw, AttackDef *a, unsigned *speed)
{
    *a = *raw;
    *speed = 1000;
    if (owner >= TOWER_LIMIT)
        return;
    Tower *t = &game.towers[owner];
    const TowerProfile *p = tower_profile(t);
    unsigned rate = t->support_rate ? t->support_rate : 1000;
    unsigned range = t->support_range ? t->support_range : 1000;
    unsigned pierce = pierce_factor(t, raw);

    if (!(raw->flags & A_SENTRY) && t->buff_clock && t->buff_shots) {
        if (attack_has_damage(raw))
            a->damage = bounded(a->damage + t->brew_damage);
        a->pierce = bounded(a->pierce + t->brew_pierce);
        rate = rate * (t->brew_rate ? t->brew_rate : 1000) / 1000;
        range = range * (t->brew_range ? t->brew_range : 1000) / 1000;
    }
    if (t->type == 11 && p && p->tiers[2] == 4 && attack_has_damage(raw))
        a->damage = bounded(a->damage + necromancy_damage(owner));
    a->range = bounded((unsigned)a->range * range / 1000);
    a->pierce = bounded((unsigned)a->pierce * pierce / 1000);
    a->immunity &= ~t->support_immunity;

    if (t->support_camo)
        a->camo = 1;

    if (!(raw->flags & A_SENTRY) && t->acid_shots) {
        a->immunity &= ~IMM_LEAD;
        a->ceramic++;
        a->moab++;
    }

    if (!(raw->flags & A_SENTRY) && p && game.running && p->round_start_rate &&
        game.round_time < p->round_start_time)
        rate = rate * p->round_start_rate / 1000;

    if (!rate)
        rate = 1;
    *speed = 1000000 / rate;

    if (p) {
        unsigned increase = t->wrath;
        if (p->life_speed_per || p->life_speed_base) {
            unsigned lost =
                t->placement_lives > t->lowest_lives ? t->placement_lives - t->lowest_lives : 0;
            if (lost > p->life_speed_cap)
                lost = p->life_speed_cap;
            *speed =
                (uint64_t)*speed * (1000 + p->life_speed_base + lost * p->life_speed_per) / 1000;
        }
        if (increase)
            *speed = (uint64_t)*speed * (1000 + increase) / 1000;
    }
}

void support_shot(unsigned owner, const AttackDef *raw, Shot *shot)
{
    AttackDef a;
    unsigned speed;
    support_attack(owner, raw, &a, &speed);
    // Snapshot potion buffs so later source changes cannot alter emitted projectiles.
    shot->damage = a.damage;
    shot->pierce = a.pierce ? a.pierce : 1;
    shot->immunity = a.immunity;
    shot->camo = a.camo;
    shot->damage_bonus = owner < TOWER_LIMIT && !(raw->flags & A_SENTRY) &&
                                 game.towers[owner].buff_clock && game.towers[owner].buff_shots
                             ? game.towers[owner].brew_damage
                             : 0;
    if (owner < TOWER_LIMIT && game.towers[owner].type == 11 && attack_has_damage(raw))
        shot->damage_bonus += necromancy_damage(owner);
    shot->pierce_factor = owner < TOWER_LIMIT ? pierce_factor(&game.towers[owner], raw) : 1000;
    shot->brew_pierce = owner < TOWER_LIMIT && !(raw->flags & A_SENTRY) &&
                                game.towers[owner].buff_clock && game.towers[owner].buff_shots
                            ? game.towers[owner].brew_pierce
                            : 0;
    shot->pierce_bonus = a.pierce > raw->pierce ? a.pierce - raw->pierce : 0;
    shot->immunity_removed =
        owner < TOWER_LIMIT
            ? (game.towers[owner].support_immunity |
               (!(raw->flags & A_SENTRY) && game.towers[owner].acid_shots ? IMM_LEAD : 0))
            : 0;
    shot->ceramic_bonus = a.ceramic - raw->ceramic;
    shot->moab_bonus = a.moab - raw->moab;
    shot->fortified_lead_bonus =
        owner < TOWER_LIMIT && !(raw->flags & A_SENTRY) && game.towers[owner].acid_shots ? 1 : 0;

    if (owner >= TOWER_LIMIT)
        return;
    Tower *t = &game.towers[owner];
    const TowerProfile *p = tower_profile(t);
    unsigned weapon = 0;

    for (unsigned i = 0; p && i < p->attack_count; i++)
        if (raw == &p->attacks[i]) {
            weapon = i;
            break;
        }
    unsigned base = raw->pierce;

    if (!(raw->flags & A_SENTRY) && t->buff_clock && t->buff_shots)
        base += t->brew_pierce;
    unsigned fraction = base * pierce_factor(t, raw) % 1000;
    unsigned remainder = t->pierce_remainder[weapon] + fraction;
    shot->pierce = bounded(shot->pierce + remainder / 1000);
    t->pierce_remainder[weapon] = remainder % 1000;
    shot->pierce_bonus = shot->pierce > raw->pierce ? shot->pierce - raw->pierce : 0;
}

void support_fired(unsigned owner)
{
    if (owner >= TOWER_LIMIT)
        return;
    Tower *t = &game.towers[owner];
    if (t->buff_clock && t->buff_shots)
        t->buff_shots--;

    if (t->acid_shots)
        t->acid_shots--;
}

static int potion_target(unsigned owner, const TowerProfile *p, int brew)
{
    Tower *t = &game.towers[owner];
    int result = -1;
    unsigned count = 0;
    int32_t best = INT32_MAX;
    int unbuffed = 0;
    unsigned radius = own_range(t, p) * (t->support_range ? t->support_range : 1000) / 1000;

    for (unsigned j = 0; j < TOWER_LIMIT; j++) {
        Tower *v = &game.towers[j];
        const TowerProfile *vp = tower_profile(v);
        if (j == owner || !v->active || !vp || !vp->attack_count || v->type == 14 ||
            v->type == 16 || v->type == 18 || !nearby(t, v, radius))
            continue;
        if (!brew && v->type == 5 && vp->tiers[0] < 2)
            continue;
        if (brew) {
            if (v->brew_block ||
                (v->buff_clock && v->buff_shots && v->brew_pierce >= p->buff_pierce))
                continue;
            int32_t distance = distance_squared(t->x, t->y, v->x, v->y);
            if (distance < best) {
                best = distance;
                result = j;
            }
        } else if (v->acid_shots < p->acid_cap) {
            if (!v->acid_shots && !unbuffed) {
                unbuffed = 1;
                count = 0;
                result = -1;
            }
            if (unbuffed && v->acid_shots)
                continue;
            if (game_random() % ++count == 0)
                result = j;
        }
    }
    return result;
}

static uint32_t flight_time(const Tower *source, const Tower *target, unsigned speed)
{
    unsigned distance =
        integer_sqrt(distance_squared(source->x, source->y, target->x, target->y)) * 16;
    return speed ? ((uint64_t)distance * 6000 + speed - 1) / speed : TICK;
}

static void apply_potion(unsigned owner, const TowerProfile *p, int brew)
{
    Tower *t = &game.towers[owner];
    unsigned id = brew ? t->brew_target : t->acid_target;
    if (id >= TOWER_LIMIT || !game.towers[id].active)
        return;
    Tower *v = &game.towers[id];

    if (brew) {
        if (v->brew_block || (v->buff_clock && v->buff_shots && v->brew_pierce > p->buff_pierce))
            return;
        v->alchemist = owner + 1;
        v->buff_clock = p->buff_time;
        v->buff_shots = p->buff_shots;
        v->brew_block = p->buff_block_time;
        v->brew_damage = p->buff_damage;
        v->brew_pierce = p->buff_pierce;
        v->brew_rate = p->buff_speed;
        v->brew_range = p->buff_range_multiplier;
    } else {
        v->acid_shots = bounded(v->acid_shots + p->acid_shots);
        if (v->acid_shots > p->acid_cap)
            v->acid_shots = p->acid_cap;
    }
}

static int grass(int32_t x, int32_t y, unsigned radius)
{
    int sx = screen_x(x), sy = screen_y(y);
    int rx = (radius * 27 / 25 + Q - 1) / Q, ry = (radius * 14 / 15 + Q - 1) / Q;
    if (sx - rx < 0 || sx + rx >= MAP_W || sy - ry < 0 || sy + ry >= MAP_H)
        return 0;

    for (int row = sy - ry; row <= sy + ry; row++)
        for (int col = sx - rx; col <= sx + rx; col++)
            if (inside(x, y, world_x(col), world_y(row), radius) &&
                !(meadow_placement[row * 48 + col / 8] & (0x80 >> (col % 8))))
                return 0;
    return 1;
}

static int random_position(const Tower *t, unsigned minimum, unsigned maximum, unsigned radius,
                           int require_grass, int32_t *x, int32_t *y)
{
    for (unsigned n = 0; n < 128; n++) {
        int32_t dx = (int32_t)(game_random() % (maximum * 2 + 1)) - maximum;
        int32_t dy = (int32_t)(game_random() % (maximum * 2 + 1)) - maximum;
        unsigned d = distance_squared(dx, dy, 0, 0);
        if (d < (minimum / 16) * (minimum / 16) || d > (maximum / 16) * (maximum / 16))
            continue;
        *x = t->x + dx;
        *y = t->y + dy;
        if (*x < -143 * Q || *x > 143 * Q || *y < -109 * Q || *y > 109 * Q)
            continue;
        if (!require_grass || grass(*x, *y, radius))
            return 1;
    }
    return 0;
}

static int spawn_sentry(unsigned owner, const TowerProfile *p)
{
    if (!p->sub_profiles || !p->sub_profile_count)
        return 0;
    Tower *t = &game.towers[owner];
    int32_t x, y;

    if (!random_position(t, p->sub_min_radius, p->sub_max_radius, p->sub_placement_radius, 1, &x,
                         &y))
        return 0;

    for (unsigned i = 0; i < SENTRY_LIMIT; i++)
        if (!game.sentries[i].active) {
            Sentry *s = &game.sentries[i];
            memset(s, 0, sizeof *s);
            s->active = 1;
            s->owner = owner;
            s->x = x;
            s->y = y;
            s->life = p->sub_life;
            s->profile = &p->sub_profiles[game_random() % p->sub_profile_count];
            unsigned distance = integer_sqrt(distance_squared(t->x, t->y, x, y)) * 16;
            s->flight = (uint64_t)distance * 6000 / (80 * Q);
            game.sentry_count++;
            return 1;
        }
    game.pool_full = 1;
    return 0;
}

static int drop_cash(unsigned owner, const TowerProfile *p)
{
    Tower *t = &game.towers[owner];
    int32_t x, y;
    if (!random_position(t, p->income_min_radius, p->income_max_radius, 0, 0, &x, &y))
        return 0;

    for (unsigned i = 0; i < CASH_DROP_LIMIT; i++)
        if (!game.cash_drops[i].active) {
            CashDrop *drop = &game.cash_drops[i];
            drop->active = 1;
            drop->x = x;
            drop->y = y;
            drop->amount = p->income;
            drop->life = p->income_life;
            game.cash_drop_count++;
            return 1;
        }
    game.pool_full = 1;
    return 0;
}

void support_tick(unsigned owner, const TowerProfile *p)
{
    Tower *t = &game.towers[owner];
    if (!t->support_initialised) {
        t->support_initialised = 1;
        t->placement_lives = t->lowest_lives = t->previous_lives = game.lives;
        t->wrath_pops = t->pops;
        t->acid_target = t->brew_target = UINT16_MAX;
    }

    if (game.lives > t->previous_lives)
        t->lives_gained = bounded(t->lives_gained + game.lives - t->previous_lives);
    t->previous_lives = game.lives;

    if (game.lives < t->lowest_lives)
        t->lowest_lives = game.lives;

    if (p->wrath_threshold) {
        if (t->pops != t->wrath_pops) {
            unsigned damage = t->pops - t->wrath_pops;
            t->wrath_pops = t->pops;
            t->wrath_idle = 0;
            t->support_clock += damage;
            unsigned stacks = t->support_clock / p->wrath_threshold;
            if (stacks > p->wrath_max_stacks)
                stacks = p->wrath_max_stacks;
            t->wrath = stacks * p->wrath_increment;
        } else {
            t->wrath_idle += TICK;
            if (t->wrath_idle >= p->wrath_timeout)
                t->wrath = t->support_clock = 0;
        }
    }

    if (t->acid_flight) {
        t->acid_flight = timer(t->acid_flight);
        if (!t->acid_flight)
            apply_potion(owner, t->acid_profile ? t->acid_profile : p, 0);
    }

    if (t->brew_flight) {
        t->brew_flight = timer(t->brew_flight);
        if (!t->brew_flight)
            apply_potion(owner, t->brew_profile ? t->brew_profile : p, 1);
    }

    if (!game.running)
        return;

    if (p->support & S_ALCHEMIST) {
        unsigned rate = t->support_rate ? t->support_rate : 1000;
        unsigned elapsed = TICK * 1000 / rate;
        t->acid_clock = t->acid_clock > elapsed ? t->acid_clock - elapsed : 0;
        t->brew_clock = t->brew_clock > elapsed ? t->brew_clock - elapsed : 0;
        for (int brew = 0; brew <= 1; brew++) {
            unsigned period = brew ? p->buff_period : p->acid_period;
            uint32_t *clock = brew ? &t->brew_clock : &t->acid_clock;
            uint32_t *flight = brew ? &t->brew_flight : &t->acid_flight;
            if (!period || *clock || *flight)
                continue;
            int target = potion_target(owner, p, brew);
            if (target < 0)
                continue;
            if (brew) {
                t->brew_target = target;
                t->brew_profile = p;
            } else {
                t->acid_target = target;
                t->acid_profile = p;
            }
            *flight = flight_time(t, &game.towers[target], p->buff_throw_speed);
            if (!*flight)
                *flight = TICK;
            *clock = period;
        }
    }

    if (p->support & S_SENTRY) {
        AttackDef effective;
        unsigned speed;
        support_attack(owner, &p->attacks[0], &effective, &speed);
        unsigned elapsed = (uint64_t)TICK * speed / 1000;
        t->support_clock = t->support_clock > elapsed ? t->support_clock - elapsed : 0;
        if (!t->support_clock && spawn_sentry(owner, p)) {
            t->support_clock = p->sub_period;
            support_fired(owner);
        }
    }

    if (p->support & S_FARM) {
        if (t->farm_round != game.round) {
            t->farm_round = game.round;
            t->farm_emitted = 0;
            t->support_clock = 0;
        }
        t->support_clock = timer(t->support_clock);
        unsigned end = game_round()->end;
        unsigned wanted = end ? (uint64_t)game.round_time * p->income_count / end : p->income_count;
        unsigned cap = p->income_count;
        if (wanted > cap)
            wanted = cap;
        if (t->farm_emitted < wanted && !t->support_clock && drop_cash(owner, p)) {
            t->farm_emitted++;
            t->support_clock = p->income_period;
        }
    }
}

void support_units_tick(void)
{
    unsigned seen = 0, pending = game.cash_drop_count;
    for (unsigned i = 0; i < CASH_DROP_LIMIT && seen < pending; i++) {
        CashDrop *drop = &game.cash_drops[i];
        if (!drop->active)
            continue;
        seen++;
        drop->life = timer(drop->life);
        if (!drop->life) {
            drop->active = 0;
            game.cash_drop_count--;
        }
    }
    seen = 0;
    pending = game.sentry_count;

    for (unsigned i = 0; i < SENTRY_LIMIT && seen < pending; i++) {
        Sentry *s = &game.sentries[i];
        if (!s->active)
            continue;
        seen++;
        Tower *owner = &game.towers[s->owner];
        if (!owner->active) {
            s->active = 0;
            game.sentry_count--;
            continue;
        }
        if (s->flight) {
            s->flight = timer(s->flight);
            continue;
        }
        s->life = timer(s->life);
        if (!s->life) {
            s->active = 0;
            game.sentry_count--;
            continue;
        }
        if (!game.running || !s->profile->attack_count)
            continue;
        const AttackDef *raw = &s->profile->attacks[0];
        AttackDef effective;
        unsigned speed;
        support_attack(s->owner, raw, &effective, &speed);
        unsigned elapsed = (uint64_t)TICK * speed / 1000;
        if (s->clock > elapsed) {
            s->clock -= elapsed;
            continue;
        }
        int target = tower_target(owner, &effective, s->x, s->y);
        if (target < 0)
            continue;
        attack_emit(s->owner, raw, s->x, s->y, target);
        s->clock = raw->period > elapsed - s->clock ? raw->period - (elapsed - s->clock) : 0;
    }
}

unsigned support_collect(int32_t x, int32_t y, unsigned radius)
{
    unsigned collected = 0;
    for (unsigned i = 0; i < CASH_DROP_LIMIT; i++) {
        CashDrop *drop = &game.cash_drops[i];
        if (!drop->active || !inside(x, y, drop->x, drop->y, radius))
            continue;
        unsigned cash = drop->amount * 100u;
        game.cash += cash;
        collected += cash;
        drop->active = 0;
        game.cash_drop_count--;
    }
    return collected;
}

int support_blocks_regrow(const Bloon *b)
{
    for (unsigned i = 0; i < TOWER_LIMIT; i++) {
        Tower *t = &game.towers[i];
        const TowerProfile *p = tower_profile(t);
        if (!t->active || !p || !p->regrow_block_range)
            continue;
        unsigned radius =
            p->regrow_block_range * (t->support_range ? t->support_range : 1000) / 1000;
        if (inside(t->x, t->y, b->x, b->y, radius))
            return 1;
    }
    return 0;
}

void support_round_end(void)
{
    // Short rounds still emit the full banana budget.
    for (unsigned owner = 0; owner < TOWER_LIMIT; owner++) {
        Tower *t = &game.towers[owner];
        const TowerProfile *p = tower_profile(t);
        if (!t->active || !p || !(p->support & S_FARM))
            continue;
        if (t->farm_round != game.round) {
            t->farm_round = game.round;
            t->farm_emitted = 0;
        }
        unsigned cap = p->income_count;
        while (t->farm_emitted < cap && drop_cash(owner, p))
            t->farm_emitted++;
    }
}
