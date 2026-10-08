#include "game.h"

// Trap capacity counts remaining layers; shell hit points do not pay extra cash.
static unsigned subtree_rbe(unsigned type, unsigned flags, unsigned hp)
{
    const BloonDef *d = &bloon_defs[type];
    unsigned n = hp;
    for (unsigned k = 0; k < 2; k++)
        if (d->count[k]) {
            unsigned c = d->child[k], fort = (flags & FORTIFIED) && c >= CERAMIC;
            n += d->count[k] * subtree_rbe(c, fort ? FORTIFIED : 0,
                                           fort ? bloon_defs[c].fort_hp : bloon_defs[c].hp);
        }
    return n;
}

static unsigned subtree_cash(unsigned type, unsigned paid, unsigned branch, int disabled)
{
    if (disabled)
        return 0;
    const BloonDef *d = &bloon_defs[type];
    unsigned bit = 1u << type;
    unsigned n = (paid & bit) ? 0 : 1, index = 0, retained = (branch & bit) ? 1 : 0;

    for (unsigned k = 0; k < 2; k++)
        for (unsigned j = 0; j < d->count[k]; j++, index++)
            n += subtree_cash(d->child[k], paid | bit, branch, (paid & bit) && index != retained);
    return n;
}

int trap_emit_allowed(unsigned owner, const AttackDef *a)
{
    unsigned count = 0;
    for (unsigned n = 0; n < game.shot_count; n++) {
        Shot *s = &game.shots[game.shot_active[n]];
        if (s->active && s->owner == owner && (s->attack->flags & A_TRAP) && !s->trap_full)
            count++;
    }
    return count < (a->projectile_limit ? a->projectile_limit : 1);
}

int trap_capture(Shot *s, unsigned enemy)
{
    Bloon *b = &game.bloons[enemy];
    const AttackDef *a = s->attack;
    if (b->type >= MOAB || s->trap_full || s->trap_close)
        return 0;
    unsigned rbe = subtree_rbe(b->type, b->flags, b->hp);
    unsigned capacity = a->trap_capacity ? a->trap_capacity : 500;
    unsigned payable =
        subtree_cash(b->type, b->regrow_paid_layers, b->regrow_branch, b->cash_disabled);
    s->trap_used += rbe;
    s->trap_cash += payable * (game.round <= 50 ? 100 : 50) *
                    (a->trap_cash_multiplier ? a->trap_cash_multiplier : 2000) / 1000;

    if (!b->cash_disabled)
        game.towers[s->owner].pops += rbe;
    b->active = 0;
    game.bloon_count--;

    if (s->trap_used >= capacity)
        s->trap_close = a->trap_close_delay ? a->trap_close_delay : TICK;
    return 1;
}

void trap_tick(Shot *s)
{
    if (!game.towers[s->owner].active) {
        shot_release(s);
        return;
    }

    if (s->trap_close) {
        s->trap_close = s->trap_close > TICK ? s->trap_close - TICK : 0;
        if (!s->trap_close) {
            s->trap_full = 1;
            s->life = s->age + 30000;
            Tower *t = &game.towers[s->owner];
            const TowerProfile *p = tower_profile(t);
            for (unsigned j = 0; p && j < p->attack_count; j++)
                if (p->attacks[j].flags & A_TRAP)
                    t->clocks[j] = s->attack->projectile_limit_delay;
        }
    }
}

unsigned trap_collect(int32_t x, int32_t y)
{
    unsigned cash = 0;
    for (unsigned n = 0; n < game.shot_count;) {
        Shot *s = &game.shots[game.shot_active[n]];
        if (!s->trap_full) {
            n++;
            continue;
        }
        unsigned radius = 100 * Q;
        for (unsigned j = 0; j < s->attack->child_count; j++)
            if (s->attack->children[j].collect_radius)
                radius = s->attack->children[j].collect_radius;
        if (distance_squared(x, y, s->x, s->y) > (int32_t)(radius / 16) * (int32_t)(radius / 16)) {
            n++;
            continue;
        }
        cash += s->trap_cash;
        shot_release(s);
    }
    game.cash += cash;
    return cash;
}
