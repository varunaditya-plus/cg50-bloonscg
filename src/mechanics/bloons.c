#include "game.h"
#include <stdlib.h>
#include <string.h>

// https://github.com/KyleDerZweite/btd6-atlas/tree/ded3155921d70cd83d803b4d70022000e4c37c6b
#define GROW_PERIOD 18000u
#define GROW_FORTIFIED 128u
#define CASCADE_STATUS (1u << 31)
static uint16_t allocation_hint, reserved_children;
static unsigned cached_path_segment;
static uint32_t active_words[(BLOON_LIMIT + 31) / 32];
static struct {
    uint16_t id, generation;
} tick_bloons[BLOON_LIMIT];
static const uint8_t lowest_bit[32] = {0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8, 31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9};
static const uint8_t layer_number[BLOON_TYPES] = {1, 2, 3, 4, 5, 6, 6, 6, 7, 7, 8, 9, 10, 11};

static int32_t path_offset(int32_t numerator, const PathPoint *point, uint32_t span)
{
    uint32_t magnitude = numerator < 0 ? -(uint32_t)numerator : (uint32_t)numerator;
    uint32_t quotient = (uint64_t)magnitude * point->reciprocal >> 32;
    // A rounded-up reciprocal can overshoot the exact truncated quotient by one.
    if ((uint64_t)quotient * span > magnitude)
        quotient--;
    return numerator < 0 ? -(int32_t)quotient : (int32_t)quotient;
}

void path_position(int32_t distance, int32_t *x, int32_t *y)
{
    if (distance <= 0) {
        *x = meadow_path[0].x;
        *y = meadow_path[0].y;
        return;
    }
    unsigned last = meadow_path_count - 1;

    if ((uint32_t)distance >= meadow_path_length) {
        *x = meadow_path[last].x;
        *y = meadow_path[last].y;
        return;
    }
    unsigned lo = cached_path_segment;
    if ((uint32_t)distance < meadow_path[lo].distance ||
        (uint32_t)distance >= meadow_path[lo + 1].distance) {
        unsigned hi = last;
        lo = 0;
        while (hi - lo > 1) {
            unsigned mid = (lo + hi) / 2;
            if (meadow_path[mid].distance <= (uint32_t)distance)
                lo = mid;
            else
                hi = mid;
        }
        cached_path_segment = lo;
    }
    const PathPoint *a = &meadow_path[lo], *b = &meadow_path[lo + 1];
    int32_t offset = distance - a->distance, span = b->distance - a->distance;
    *x = a->x + path_offset((b->x - a->x) * offset, a, span);
    *y = a->y + path_offset((b->y - a->y) * offset, a, span);
}

static uint32_t countdown(uint32_t timer)
{
    return timer > TICK ? timer - TICK : 0;
}

int bloon_next(unsigned start)
{
    unsigned word = start / 32;
    if (word >= sizeof active_words / sizeof *active_words)
        return -1;
    uint32_t bits = active_words[word] & (UINT32_MAX << (start % 32));

    while (!bits) {
        if (++word >= sizeof active_words / sizeof *active_words)
            return -1;
        bits = active_words[word];
    }
    // Multiplying the isolated lowest bit gives a unique five-bit table index.
    unsigned index = ((bits & -bits) * 0x077cb531u) >> 27;
    return word * 32 + lowest_bit[index];
}

void bloons_reset_cache(void)
{
    memset(active_words, 0, sizeof active_words);
    unsigned seen = 0;
    for (unsigned i = 0; i < BLOON_LIMIT && seen < game.bloon_count; i++) {
        if (game.bloons[i].active) {
            active_words[i / 32] |= 1u << (i % 32);
            seen++;
        }
    }
}

void bloon_remove(Bloon *b)
{
    unsigned id = b - game.bloons;
    active_words[id / 32] &= ~(1u << (id % 32));
    b->active = 0;
    if (game.bloon_count)
        game.bloon_count--;
}

static unsigned child_flags(const Bloon *parent, unsigned child)
{
    unsigned flags = parent->flags & (CAMO | REGROW);
    if ((parent->flags & FORTIFIED) && child >= CERAMIC)
        flags |= FORTIFIED;
    return flags;
}

int bloon_spawn(unsigned type, unsigned flags, int32_t distance, unsigned max_regrow)
{
    if (type >= BLOON_TYPES)
        return -1;
    if (game.bloon_count + reserved_children >= BLOON_LIMIT) {
        game.pool_full = 1;
        return -1;
    }

    for (unsigned n = 0; n < BLOON_LIMIT; n++) {
        unsigned id = (allocation_hint + n) % BLOON_LIMIT;
        Bloon *b = &game.bloons[id];
        if (b->active)
            continue;
        uint16_t generation = b->generation + 1;
        if (!generation)
            generation = 1;
        memset(b, 0, sizeof *b);
        b->generation = generation;
        b->active = 1;
        active_words[id / 32] |= 1u << (id % 32);
        b->type = type;
        if (type >= MOAB)
            flags &= ~REGROW;
        if (type != LEAD && type < CERAMIC)
            flags &= ~FORTIFIED;
        b->flags = flags;
        b->regrow_max = max_regrow < BLOON_TYPES ? max_regrow : type;
        if (flags & FORTIFIED)
            b->regrow_max |= GROW_FORTIFIED;
        b->hp = (flags & FORTIFIED) ? bloon_defs[type].fort_hp : bloon_defs[type].hp;
        b->distance = distance < 0 ? 0 : distance;
        b->slow = b->moab_slow = b->ice_slow = 1000;
        b->pop_source = TOWER_LIMIT;
        path_position(b->distance, &b->x, &b->y);
        game.bloon_count++;
        allocation_hint = (id + 1) % BLOON_LIMIT;
        return id;
    }
    game.pool_full = 1;
    return -1;
}

void bloon_clear_glue_ice(Bloon *b)
{
    b->glue = b->glue_level = b->glue_layers = 0;
    b->slow = b->moab_slow = 1000;
    b->freeze = b->freeze_layers = 0;
    b->ice_slow = 1000;
    b->ice_slow_time = b->ice_slow_layers = 0;

    for (unsigned i = 0; i < DOT_LIMIT; i++) {
        DotStatus *d = &b->dots[i];
        if (d->mutation == MUT_GLUE || d->mutation == MUT_CORROSIVE || d->mutation == MUT_DISSOLVER)
            memset(d, 0, sizeof *d);
    }
}

static unsigned dot_family(const AttackEffect *e, unsigned owner)
{
    if (e->value > 0 && e->value <= 65535)
        return e->value;
    unsigned type = owner < TOWER_LIMIT ? game.towers[owner].type + 1 : MONKEY_COUNT + 1;
    return (type << 8) | e->mutation;
}

static int apply_dot(unsigned id, const AttackEffect *e, unsigned owner, const Shot *snapshot)
{
    Bloon *b = &game.bloons[id];
    unsigned family = dot_family(e, owner), free_slot = DOT_LIMIT, slot = DOT_LIMIT;
    for (unsigned i = 0; i < DOT_LIMIT; i++) {
        if (b->dots[i].time && b->dots[i].family == family) {
            slot = i;
            break;
        }
        if (!b->dots[i].time && free_slot == DOT_LIMIT)
            free_slot = i;
    }
    unsigned fresh = slot == DOT_LIMIT;

    if (fresh)
        slot = free_slot;

    if (slot == DOT_LIMIT) {
        game.pool_full = 1;
        return 1;
    }
    DotStatus *d = &b->dots[slot];
    unsigned period = e->period ? e->period : 6000;

    if (!fresh) {
        if (e->flags & EF_NO_REFRESH)
            return 1;
        // Compare rational DPS without resetting a stronger mutation cadence.
        if ((uint64_t)e->damage * d->period < (uint64_t)d->damage * period)
            return 1;
    } else {
        memset(d, 0, sizeof *d);
        d->clock = period;
    }
    d->time = e->time;
    d->period = period;
    d->damage = e->damage;
    d->immunity = e->immunity & ~(snapshot ? snapshot->immunity_removed : 0);
    d->owner = owner;
    d->layers = e->layers ? e->layers : 65535;
    d->family = family;
    d->flags = e->flags;
    d->mutation = e->mutation;
    d->moab_damage = e->auxiliary[0] + (snapshot ? snapshot->moab_bonus : 0);
    d->ceramic_damage = e->auxiliary[1] + (snapshot ? snapshot->ceramic_bonus : 0);
    d->fortified_damage = e->auxiliary[2];
    d->camo_damage = e->auxiliary[3];
    d->fortified_lead_damage = snapshot ? snapshot->fortified_lead_bonus : 0;

    if (fresh && (e->flags & EF_IMMEDIATE)) {
        unsigned generation = b->generation;
        bloon_damage(id, e->damage, e->immunity, owner);
        return b->active && b->generation == generation;
    }
    return 1;
}

static void inherit(Bloon *child, const Bloon *parent, unsigned branch, uint32_t parent_token)
{
    uint16_t generation = child->generation;
    uint16_t hp = child->hp;
    uint8_t type = child->type, flags = child->flags;
    *child = *parent;
    child->active = 1;
    child->generation = generation;
    child->hp = hp;
    child->type = type;
    child->flags = flags;
    // Only the original split branch retains the unpaid subtree after regrowing.
    unsigned bit = 1u << parent->type;
    unsigned retained = (parent->regrow_branch & bit) ? 1 : 0;

    if ((parent->regrow_paid_layers & bit) && branch != retained)
        child->cash_disabled = 1;
    child->regrow_paid_layers |= bit;

    if (branch)
        child->regrow_branch |= bit;
    else
        child->regrow_branch &= ~bit;

    if (child->strip_layers)
        child->strip_layers--;

    if (!child->strip_layers)
        child->strip_time = child->property_strip = 0;

    if (child->freeze_layers)
        child->freeze_layers--;

    if (!child->freeze_layers)
        child->freeze = 0;

    if (child->ice_slow_layers)
        child->ice_slow_layers--;

    if (!child->ice_slow_layers)
        child->ice_slow = 1000;

    if (child->glue_layers)
        child->glue_layers--;

    if (!child->glue_layers) {
        child->glue = child->glue_level = 0;
        child->slow = child->moab_slow = 1000;
    }

    for (unsigned i = 0; i < DOT_LIMIT; i++) {
        DotStatus *d = &child->dots[i];
        if (!(d->flags & EF_PERSIST_DEGRADE) && d->layers)
            d->layers--;
        if (!d->layers || (parent->type >= MOAB && !(d->flags & EF_CASCADE)))
            d->time = 0;
    }

    if (child->stun_layers)
        child->stun_layers--;

    if (!child->stun_layers)
        child->stun = 0;

    if (parent->type >= MOAB && !(parent->status & CASCADE_STATUS)) {
        child->glue = child->freeze = child->stun = 0;
        child->slow = child->moab_slow = child->ice_slow = 1000;
        child->vulnerability = child->vulnerable_damage = 0;
        child->property_strip = child->strip_time = 0;
    }

    if (parent->type >= MOAB)
        child->knockback = 0;

    if (parent->type >= MOAB)
        child->wind_remaining = 0;

    if (child->ancestor_count == 16) {
        memmove(child->ancestors, child->ancestors + 1, 15 * sizeof child->ancestors[0]);
        child->ancestor_count = 15;
    }
    child->ancestors[child->ancestor_count++] = parent_token;
}

typedef struct DamageFrame {
    struct DamageFrame *previous;
    Bloon parent;
    CombatPop *pop;
    uint32_t parent_token;
    unsigned id, immunity, owner, credit, excess, children;
    unsigned group, child, branch;
    uint8_t spawning, add_credit, pooled;
} DamageFrame;

static DamageFrame damage_frames[16];
static unsigned damage_depth;

static DamageFrame *damage_acquire(void)
{
    DamageFrame *frame = damage_depth < 16 ? &damage_frames[damage_depth]
                                           : malloc(sizeof *frame);
    if (!frame) {
        game.pool_full = 1;
        return NULL;
    }
    frame->pooled = damage_depth < 16;
    damage_depth++;
    return frame;
}

static void damage_release(DamageFrame *frame)
{
    damage_depth--;
    if (!frame->pooled)
        free(frame);
}

static DamageFrame *damage_begin(unsigned id, unsigned damage, unsigned immunity, unsigned owner,
                                  unsigned *result)
{
    *result = 0;
    if (id >= BLOON_LIMIT || !damage)
        return NULL;
    Bloon *b = &game.bloons[id];
    if (!b->active)
        return NULL;
    const BloonDef *def = &bloon_defs[b->type];
    unsigned properties = (def->immunity | (b->freeze ? IMM_FROZEN : 0)) & ~b->property_strip;

    if (properties & immunity)
        return NULL;
    unsigned removed = damage < b->hp ? damage : b->hp;
    unsigned payable = !b->cash_disabled && !(b->regrow_paid_layers & (1u << b->type));
    unsigned credit = payable ? removed : 0;

    if (damage < b->hp) {
        b->hp -= damage;
        if (owner < TOWER_LIMIT)
            game.towers[owner].pops += credit;
        *result = credit;
        return NULL;
    }
    unsigned children = def->count[0] + def->count[1];

    if (game.bloon_count - 1 + children + reserved_children > BLOON_LIMIT) {
        game.pool_full = 1;
        removed = b->hp > 1 ? b->hp - 1 : 0;
        credit = payable ? removed : 0;
        b->hp -= removed;
        if (owner < TOWER_LIMIT)
            game.towers[owner].pops += credit;
        *result = credit;
        return NULL;
    }

    DamageFrame *frame = NULL;
    if (children || b->pop_attack) {
        frame = damage_acquire();
        if (!frame)
            return NULL;
        frame->previous = NULL;
        frame->parent = *b;
        frame->parent_token = ((uint32_t)b->generation << 16) | id;
        frame->id = id;
        frame->immunity = immunity;
        frame->owner = owner;
        frame->credit = credit;
        frame->excess = b->type < MOAB ? damage - removed : 0;
        frame->children = children;
        frame->group = frame->child = frame->branch = 0;
        frame->spawning = frame->add_credit = 0;
    }

    bloon_remove(b);
    if (payable)
        game.cash += game.round <= 50 ? 100 : 50;
    if (owner < TOWER_LIMIT)
        game.towers[owner].pops += credit;

    if (frame) {
        reserved_children += children;
        frame->pop = combat_pop_begin(&frame->parent);
    } else {
        allocation_hint = id;
        *result = credit;
    }
    return frame;
}

int bloon_damage(unsigned id, unsigned damage, unsigned immunity, unsigned owner)
{
    unsigned credit;
    DamageFrame *frame = damage_begin(id, damage, immunity, owner, &credit);
    if (!frame)
        return credit;

    // A LIFO continuation finishes each pop blast and descendant before its next sibling.
    for (;;) {
        if (!frame->spawning) {
            unsigned enemy, amount, mask;
            if (frame->pop && combat_pop_next(frame->pop, &enemy, &amount, &mask)) {
                DamageFrame *next = damage_begin(enemy, amount, mask, frame->parent.pop_source,
                                                   &credit);
                if (next) {
                    next->previous = frame;
                    frame = next;
                }
                continue;
            }
            combat_pop_finish(frame->pop);
            frame->pop = NULL;
            reserved_children -= frame->children;
            allocation_hint = frame->id;
            frame->spawning = 1;
        }

        const BloonDef *def = &bloon_defs[frame->parent.type];
        while (frame->group < 2 && frame->child >= def->count[frame->group]) {
            frame->group++;
            frame->child = 0;
        }
        if (frame->group < 2) {
            unsigned type = def->child[frame->group];
            unsigned branch = frame->branch++;
            frame->child++;
            int child_id = bloon_spawn(type, child_flags(&frame->parent, type),
                                       frame->parent.distance,
                                       frame->parent.regrow_max & ~GROW_FORTIFIED);
            if (child_id < 0)
                continue;
            inherit(&game.bloons[child_id], &frame->parent, branch, frame->parent_token);
            if (frame->excess) {
                DamageFrame *next = damage_begin(child_id, frame->excess, frame->immunity,
                                                   frame->owner, &credit);
                if (next) {
                    next->previous = frame;
                    next->add_credit = 1;
                    frame = next;
                } else
                    frame->credit += credit;
            }
            continue;
        }

        DamageFrame *previous = frame->previous;
        unsigned result = frame->credit, add = frame->add_credit;
        damage_release(frame);
        if (!previous)
            return result;
        frame = previous;
        if (add)
            frame->credit += result;
    }
}

static int has_effect(const AttackDef *a, unsigned kind)
{
    for (unsigned i = 0; i < a->effect_count; i++)
        if (a->effects[i].kind == kind)
            return 1;
    return 0;
}

static unsigned bloon_tags(const Bloon *b)
{
    unsigned tags = 0;
    if (b->type >= MOAB)
        tags |= TAG_MOABS;
    if (b->type == MOAB)
        tags |= TAG_MOAB;

    if (b->type == BFB)
        tags |= TAG_BFB;

    if (b->type == CERAMIC)
        tags |= TAG_CERAMIC;

    if (b->type == LEAD)
        tags |= TAG_LEAD;

    if (b->type == BLACK)
        tags |= TAG_BLACK;

    if (b->type == WHITE)
        tags |= TAG_WHITE;

    if (b->type == ZEBRA)
        tags |= TAG_ZEBRA;

    if (b->flags & FORTIFIED)
        tags |= TAG_FORTIFIED;

    if (b->flags & CAMO)
        tags |= TAG_CAMO;
    return tags;
}

static int effect_matches(const AttackEffect *e, unsigned tags)
{
    if (e->exclude_tags & tags)
        return 0;
    if (e->modifier_flags & 16)
        return 1;
    int matches =
        !e->tags || ((e->modifier_flags & 8) ? (e->tags & tags) == e->tags : (e->tags & tags) != 0);
    return (e->modifier_flags & 4) ? !matches : matches;
}

static int slow_values(const AttackDef *a, const AttackEffect *e, unsigned tags,
                       unsigned *multiplier, uint32_t *time)
{
    *multiplier = e->multiplier;
    *time = e->time;
    for (unsigned i = 0; i < a->effect_count; i++) {
        const AttackEffect *m = &a->effects[i];
        if (m->kind != EF_SLOW_MODIFIER || m->mutation != e->mutation || !effect_matches(m, tags))
            continue;
        if (m->modifier_flags & 2)
            return 0;
        *multiplier = ((m->modifier_flags & 1) ? 1000 : *multiplier) * m->multiplier / 1000;
        if (m->time)
            *time = m->time;
    }
    return 1;
}

void bloon_effect_buffed(unsigned id, const AttackDef *a, unsigned owner, unsigned immunity,
                         const Shot *snapshot)
{
    if (id >= BLOON_LIMIT || !a || !game.bloons[id].active)
        return;
    Bloon *b = &game.bloons[id];
    unsigned moab = b->type >= MOAB, tags = bloon_tags(b);
    uint32_t duration = a->status_time;
    unsigned layers = a->status_layers ? a->status_layers : 1;
    unsigned properties = bloon_defs[b->type].immunity | (b->freeze ? IMM_FROZEN : 0);

    if ((properties & ~b->property_strip) & immunity)
        return;

    if (a->status_cascade)
        b->status |= CASCADE_STATUS;

    for (unsigned i = 0; i < a->child_count; i++)
        if (a->children[i].trigger == TR_POP) {
            b->pop_attack = a;
            b->pop_source = owner;
        }

    if (has_effect(a, EF_SHARDS) || has_effect(a, EF_CONCOCTION)) {
        b->pop_attack = a;
        b->pop_source = owner;
    }

    for (unsigned i = 0; i < a->effect_count; i++) {
        const AttackEffect *e = &a->effects[i];
        if (((e->flags & EF_PREVENT_MOAB) && moab) || !effect_matches(e, tags))
            continue;
        unsigned depth = e->layers ? e->layers : 65535;
        if (e->flags & EF_CASCADE)
            b->status |= CASCADE_STATUS;
        if (e->kind == EF_FREEZE && !moab) {
            if ((e->flags & EF_NO_REFRESH) && b->freeze)
                continue;
            uint32_t time = e->time;
            unsigned prevent = 0;
            for (unsigned j = 0; j < a->effect_count; j++) {
                const AttackEffect *m = &a->effects[j];
                if (m->kind == EF_FREEZE_MODIFIER && effect_matches(m, tags)) {
                    if (m->modifier_flags & 2)
                        prevent = 1;
                    time = (uint64_t)time * m->multiplier / 1000;
                }
            }
            if (prevent)
                continue;
            b->freeze = time;
            b->freeze_layers = depth;
        }
        if (e->kind == EF_SLOW) {
            unsigned multiplier;
            uint32_t time;
            if (!slow_values(a, e, tags, &multiplier, &time))
                continue;
            if (multiplier == 0) {
                if ((e->flags & EF_NO_REFRESH) && b->stun)
                    continue;
                b->stun = time;
                b->stun_layers = depth;
            } else {
                if ((e->flags & EF_NO_REFRESH) && b->ice_slow_time)
                    continue;
                b->ice_slow = multiplier;
                b->ice_slow_layers = depth;
                b->ice_slow_time = time;
            }
        }
        if (e->kind == EF_GLUE_LEVEL) {
            if (b->glue && e->value <= b->glue_level)
                continue;
            unsigned multiplier;
            uint32_t time;
            if (!slow_values(a, e, tags, &multiplier, &time))
                continue;
            b->glue = time;
            b->glue_layers = depth;
            b->glue_level = e->value;
            b->slow = multiplier;
            b->moab_slow = multiplier;
        }
        if (e->kind == EF_DOT && !apply_dot(id, e, owner, snapshot))
            return;
        if (e->kind == EF_REMOVE_DAMAGE_TYPE) {
            b->property_strip |= e->immunity;
            b->strip_time = e->time;
            b->strip_layers = depth;
        }
        if (e->kind == EF_MAIM && moab) {
            b->stun = b->type == BFB ? e->auxiliary[1] : e->auxiliary[0];
            b->stun_layers = depth;
        }
    }

    if ((a->flags & A_FREEZE) && !moab && !has_effect(a, EF_FREEZE)) {
        b->freeze = duration;
        b->freeze_layers = layers;
        if (a->slow && a->slow < 1000) {
            b->ice_slow = a->slow;
            b->ice_slow_layers = layers;
            b->ice_slow_time = UINT32_MAX;
        }
    }

    if ((a->flags & A_GLUE) && !has_effect(a, EF_GLUE_LEVEL) && (!moab || a->moab_slow)) {
        b->glue = duration;
        b->glue_layers = layers;
        b->slow = a->slow ? a->slow : 1000;
        b->moab_slow = a->moab_slow ? a->moab_slow : 1000;
    }

    if ((a->flags & A_STUN) && !has_effect(a, EF_MAIM)) {
        b->stun = moab ? (b->type == BFB ? a->bfb_stun : a->moab_stun) : duration;
        b->stun_layers = layers;
    }

    if ((a->flags & A_DOT) && a->dot_damage && !has_effect(a, EF_DOT)) {
        AttackEffect effect = {.kind = EF_DOT,
                               .time = duration ? duration : 6000,
                               .period = a->dot_period,
                               .damage = a->dot_damage,
                               .layers = layers,
                               .immunity = a->immunity,
                               .flags = a->status_cascade ? EF_CASCADE : 0};
        if (!apply_dot(id, &effect, owner, snapshot))
            return;
    }

    if ((a->flags & A_VULNERABLE) && !has_effect(a, EF_VULNERABLE)) {
        b->vulnerability = duration;
        b->vulnerable_damage = a->vulnerability;
    }

    if ((a->flags & A_PUSH) && !has_effect(a, EF_PUSH) && !has_effect(a, EF_KNOCKBACK)) {
        b->distance -= a->push;
        if (b->distance < 0)
            b->distance = 0;
        path_position(b->distance, &b->x, &b->y);
    }

    if ((a->flags & A_DECAMO) && !has_effect(a, EF_CLEANSE))
        b->flags &= ~CAMO;

    if ((a->flags & A_DEGROW) && !has_effect(a, EF_CLEANSE))
        b->flags &= ~REGROW;

    if ((a->flags & A_DEFORT) && !has_effect(a, EF_CLEANSE) && (b->flags & FORTIFIED)) {
        b->flags &= ~FORTIFIED;
        b->regrow_max &= ~GROW_FORTIFIED;
        unsigned extra = bloon_defs[b->type].fort_hp - bloon_defs[b->type].hp;
        b->hp = b->hp > extra ? b->hp - extra : 1;
    }
}

static void regrow(Bloon *b)
{
    unsigned type = b->type, cap = b->regrow_max & ~GROW_FORTIFIED;
    unsigned next = type;
    switch (type) {
    case RED:
        next = BLUE;
        break;
    case BLUE:
        next = GREEN;
        break;
    case GREEN:
        next = YELLOW;
        break;
    case YELLOW:
        next = PINK;
        break;
    case PINK:
        if ((b->regrow_paid_layers & (1u << WHITE)) || cap == WHITE)
            next = WHITE;
        else if ((b->regrow_paid_layers & (1u << PURPLE)) || cap == PURPLE)
            next = PURPLE;
        else
            next = BLACK;
        break;
    case BLACK:
        next = ((b->regrow_paid_layers & (1u << LEAD)) || cap == LEAD) ? LEAD : ZEBRA;
        break;
    case WHITE:
        next = ZEBRA;
        break;
    case ZEBRA:
        next = RAINBOW;
        break;
    case RAINBOW:
        next = CERAMIC;
        break;
    default:
        break;
    }

    if (next == type || cap >= BLOON_TYPES || layer_number[next] > layer_number[cap])
        return;
    b->type = next;

    if ((b->regrow_max & GROW_FORTIFIED) && next == cap)
        b->flags |= FORTIFIED;
    b->hp = (b->flags & FORTIFIED) ? bloon_defs[next].fort_hp : bloon_defs[next].hp;
}

void bloons_tick(void)
{
    unsigned count = 0;
    // Snapshot generations so split children wait until the next movement tick.
    for (int i = bloon_next(0); i >= 0; i = bloon_next(i + 1)) {
        tick_bloons[count].id = i;
        tick_bloons[count++].generation = game.bloons[i].generation;
    }
    for (unsigned n = 0; n < count; n++) {
        unsigned i = tick_bloons[n].id;
        Bloon *b = &game.bloons[i];
        if (!b->active || b->generation != tick_bloons[n].generation)
            continue;
        unsigned frozen = b->freeze != 0, stunned = b->stun != 0, knocked = b->knockback != 0;
        b->knockback = countdown(b->knockback);
        b->freeze = countdown(b->freeze);
        b->stun = countdown(b->stun);
        b->glue = countdown(b->glue);
        b->strip_time = countdown(b->strip_time);
        if (!b->strip_time)
            b->property_strip = 0;
        b->ice_slow_time = countdown(b->ice_slow_time);
        if (!b->ice_slow_time)
            b->ice_slow = 1000;
        if (!b->glue)
            b->slow = b->moab_slow = 1000;
        b->vulnerability = countdown(b->vulnerability);
        if (!b->vulnerability)
            b->vulnerable_damage = 0;
        unsigned original_generation = b->generation;
        for (unsigned j = 0; j < DOT_LIMIT; j++) {
            DotStatus *d = &b->dots[j];
            if (!d->time)
                continue;
            unsigned elapsed = d->time < TICK ? d->time : TICK;
            unsigned due = elapsed >= d->clock;
            unsigned damage = d->damage, immunity = d->immunity, owner = d->owner;
            if (b->type >= MOAB)
                damage += d->moab_damage;
            if (b->type == CERAMIC)
                damage += d->ceramic_damage;
            if (b->flags & FORTIFIED)
                damage += d->fortified_damage;
            if (b->type == LEAD && (b->flags & FORTIFIED))
                damage += d->fortified_lead_damage;
            if (b->flags & CAMO)
                damage += d->camo_damage;
            if (b->vulnerability)
                damage += b->vulnerable_damage;
            d->time = countdown(d->time);
            if (due)
                d->clock += d->period;
            d->clock = d->clock > elapsed ? d->clock - elapsed : 0;
            if (due) {
                bloon_damage(i, damage, immunity, owner);
                if (!b->active || b->generation != original_generation)
                    break;
            }
        }
        if (!b->active || b->generation != original_generation)
            continue;
        if ((b->flags & REGROW) && !frozen && !support_blocks_regrow(b)) {
            b->regrow_clock += TICK;
            if (b->regrow_clock >= GROW_PERIOD) {
                b->regrow_clock -= GROW_PERIOD;
                regrow(b);
            }
        }
        int blown = projectile_wind_step(b);
        if (blown)
            path_position(b->distance, &b->x, &b->y);
        if (!blown && !frozen && !stunned) {
            unsigned speed = bloon_defs[b->type].speed;
            unsigned glue = b->type >= MOAB ? b->moab_slow : b->slow;
            if (b->glue && glue < 1000)
                speed = speed * glue / 1000;
            if (b->ice_slow < 1000)
                speed = speed * b->ice_slow / 1000;
            int32_t velocity = speed;
            if (knocked)
                velocity = velocity * (1000 - (int32_t)b->knockback_multiplier) / 1000;
            // Carry fractional movement so slow blimps do not accumulate rounding drift.
            int32_t movement = velocity + b->move_remainder;
            b->distance += movement / 50;
            b->move_remainder = movement % 50;
            if (b->distance < 0) {
                b->distance = 0;
                b->move_remainder = 0;
            }
            path_position(b->distance, &b->x, &b->y);
        }
        if ((uint32_t)b->distance >= meadow_path_length) {
            unsigned leak =
                (b->flags & FORTIFIED) ? bloon_defs[b->type].fort_leak : bloon_defs[b->type].leak;
            game.lives = game.lives > leak ? game.lives - leak : 0;
            bloon_remove(b);
            if (!game.lives) {
                game.lost = 1;
                game.running = 0;
            }
        }
    }
}

void rounds_tick(void)
{
    if (!game.running || !game.round || game.round > 60 || game.lost)
        return;
    const RoundDef *r = &rounds[game.round - 1];
    for (unsigned i = 0; i < r->count; i++) {
        const RoundGroup *g = &round_groups[r->first + i];
        while (game.group_spawned[i] < g->count) {
            unsigned n = game.group_spawned[i];
            // Derive each rational spawn time instead of repeatedly rounding intervals.
            uint32_t when = g->start;
            if (g->count > 1)
                when += (uint64_t)(g->end - g->start) * n / (g->count - 1);
            if (game.round_time < when)
                break;
            if (bloon_spawn(g->type, g->flags, 0, g->type) < 0)
                break;
            game.group_spawned[i]++;
        }
    }
    unsigned complete = 1;

    for (unsigned i = 0; i < r->count; i++)
        if (game.group_spawned[i] < round_groups[r->first + i].count)
            complete = 0;

    if (complete && !game.bloon_count) {
        game.cash += (100 + game.round) * 100;
        support_round_end();
        projectile_round_end();
        game.running = 0;
        if (game.round == 60)
            game.won = 1;
        return;
    }
    game.round_time += TICK;
}
