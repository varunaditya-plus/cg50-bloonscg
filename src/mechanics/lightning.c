#include "game.h"
#include "animations.h"

static int closest(const Shot *s, int32_t x, int32_t y)
{
    const AttackDef *a = s->attack;
    int target = -1;
    int32_t best = INT32_MAX;
    for (int n = bloon_next(0); n >= 0; n = bloon_next(n + 1)) {
        if (!combat_eligible(s->owner, a, n) || shot_history_contains(s, n))
            continue;
        int32_t d = distance_squared(x, y, game.bloons[n].x, game.bloons[n].y);
        if (d < best && d <= (int32_t)(a->chain_range / 16) * (a->chain_range / 16)) {
            target = n;
            best = d;
        }
    }
    return target;
}

int lightning_step(unsigned id)
{
    // Both lightning frontiers share one projectile and collision ledger.
    Shot *s = &game.shots[id];
    const AttackDef *a = s->attack;
    if (!(a->flags & A_CHAIN) || !a->chain_splits)
        return 0;

    if (s->chain_clock > TICK) {
        s->chain_clock -= TICK;
        return 1;
    }
    s->chain_clock = s->chain_clock + a->chain_delay;
    s->chain_clock = s->chain_clock > TICK ? s->chain_clock - TICK : 0;

    if (!s->fork_count) {
        int target = s->target < BLOON_LIMIT ? s->target : -1;
        if (target < 0 || !combat_eligible(s->owner, a, target))
            target = closest(s, s->x, s->y);
        if (target < 0) {
            shot_release(s);
            return 1;
        }
        animation_line(FX_LIGHTNING, s->x, s->y, game.bloons[target].x, game.bloons[target].y);
        s->x = game.bloons[target].x;
        s->y = game.bloons[target].y;
        unsigned branches = a->chain_splits > 2 ? 2 : a->chain_splits;
        for (unsigned n = 0; n < branches; n++) {
            s->fork_x[n] = s->x;
            s->fork_y[n] = s->y;
        }
        s->fork_count = branches;
        shot_hit(id, target);
        return 1;
    }
    unsigned hit = 0;

    for (unsigned n = 0; n < s->fork_count && s->active; n++) {
        int target = closest(s, s->fork_x[n], s->fork_y[n]);
        if (target < 0)
            continue;
        animation_line(FX_LIGHTNING, s->fork_x[n], s->fork_y[n],
                       game.bloons[target].x, game.bloons[target].y);
        s->x = game.bloons[target].x;
        s->y = game.bloons[target].y;
        s->fork_x[n] = s->x;
        s->fork_y[n] = s->y;
        shot_hit(id, target);
        hit++;
    }

    if (!hit)
        shot_release(s);
    return 1;
}
