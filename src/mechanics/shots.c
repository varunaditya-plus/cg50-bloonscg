#include "game.h"
#include "animations.h"
#include <string.h>

enum { NO_HISTORY = UINT16_MAX };

static int transient_history(const Shot *s)
{
    // Traps consume targets and spikes reset each tick, so neither needs a ledger.
    const AttackDef *a = s->attack;
    return a &&
           ((a->flags & A_TRAP) || ((a->flags & A_SPIKE) && a->hit_reset && a->hit_reset <= TICK));
}

void shots_init(void)
{
    game.shot_free_count = SHOT_LIMIT;
    game.shot_history_free_count = SHOT_HISTORY_BLOCKS;

    for (unsigned n = 0; n < SHOT_LIMIT; n++)
        game.shot_free[n] = SHOT_LIMIT - 1 - n;

    for (unsigned n = 0; n < SHOT_HISTORY_BLOCKS; n++)
        game.shot_history_free[n] = SHOT_HISTORY_BLOCKS - 1 - n;
}

int shot_acquire(void)
{
    if (!game.shot_free_count) {
        game.pool_full = 1;
        return -1;
    }
    unsigned id = game.shot_free[--game.shot_free_count];
    shot_styles[id] = V_DART;
    Shot *s = &game.shots[id];
    memset(s, 0, sizeof *s);
    s->active = 1;
    s->history_block = NO_HISTORY;
    s->active_index = game.shot_count;
    game.shot_active[game.shot_count++] = id;
    return id;
}

void shot_history_reset(Shot *s)
{
    unsigned block = s->history_block;
    while (block < SHOT_HISTORY_BLOCKS) {
        ShotHistoryBlock *b = &game.shot_history[block];
        unsigned next = b->next;
        game.shot_history_free[game.shot_history_free_count++] = block;
        b->next = NO_HISTORY;
        b->count = 0;
        block = next;
    }
    s->history_count = 0;
    s->history_first = 0;
    s->history_block = NO_HISTORY;
}

void shot_release(Shot *s)
{
    if (!s->active)
        return;
    unsigned id = (unsigned)(s - game.shots), position = s->active_index;
    shot_history_reset(s);
    s->active = 0;
    shot_styles[id] = V_DART;
    unsigned moved = game.shot_active[--game.shot_count];

    if (position < game.shot_count) {
        game.shot_active[position] = moved;
        game.shots[moved].active_index = position;
    }
    game.shot_free[game.shot_free_count++] = id;
}

static int matches_token(uint32_t token, unsigned id)
{
    const Bloon *b = &game.bloons[id];
    if (token == ((uint32_t)b->generation << 16 | id))
        return 1;
    for (unsigned n = 0; n < b->ancestor_count; n++)
        if (token == b->ancestors[n])
            return 1;
    return 0;
}

int shot_history_contains(const Shot *s, unsigned enemy)
{
    if (enemy >= BLOON_LIMIT || !s->history_count || transient_history(s))
        return 0;
    if (matches_token(s->history_first, enemy))
        return 1;

    for (unsigned block = s->history_block; block < SHOT_HISTORY_BLOCKS;
         block = game.shot_history[block].next) {
        const ShotHistoryBlock *b = &game.shot_history[block];
        for (unsigned n = 0; n < b->count; n++)
            if (matches_token(b->tokens[n], enemy))
                return 1;
    }
    return 0;
}

static unsigned history_block_acquire(void)
{
    if (!game.shot_history_free_count) {
        game.pool_full = 1;
        return NO_HISTORY;
    }
    unsigned id = game.shot_history_free[--game.shot_history_free_count];
    ShotHistoryBlock *b = &game.shot_history[id];
    b->next = NO_HISTORY;
    b->count = 0;
    return id;
}

int shot_history_record(Shot *s, unsigned enemy)
{
    if (enemy >= BLOON_LIMIT)
        return 0;

    if (transient_history(s))
        return 1;
    // A generation token distinguishes descendants and reused pool slots.
    uint32_t token = ((uint32_t)game.bloons[enemy].generation << 16) | enemy;

    if (!s->history_count) {
        s->history_first = token;
        s->history_count = 1;
        return 1;
    }

    if (s->history_count == UINT16_MAX) {
        game.pool_full = 1;
        return 0;
    }
    unsigned block = s->history_block;

    if (block == NO_HISTORY) {
        block = history_block_acquire();
        if (block == NO_HISTORY)
            return 0;
        s->history_block = block;
    } else {
        while (game.shot_history[block].next != NO_HISTORY)
            block = game.shot_history[block].next;
        if (game.shot_history[block].count == HISTORY_LIMIT) {
            unsigned next = history_block_acquire();
            if (next == NO_HISTORY)
                return 0;
            game.shot_history[block].next = next;
            block = next;
        }
    }
    ShotHistoryBlock *b = &game.shot_history[block];
    b->tokens[b->count++] = token;
    s->history_count++;
    return 1;
}

void shots_owner_destroy(unsigned owner)
{
    for (unsigned n = 0; n < game.shot_count;) {
        Shot *s = &game.shots[game.shot_active[n]];
        if (s->owner == owner && s->attack && (s->attack->motion_flags & 512))
            shot_release(s);
        else
            n++;
    }
}
