#include "animations.h"
#include "combat_art.h"
#include <string.h>

AnimationEffect animation_effects[EFFECT_LIMIT];
TowerAnimation tower_animations[TOWER_LIMIT];
uint8_t shot_styles[SHOT_LIMIT];
unsigned animation_count;

static uint32_t pop_tiles[8];
static unsigned pop_count, event_count, replace;
static const uint16_t lifetimes[FX_COUNT] = {
    960, 1440, 960, 1440, 1440, 600, 960, 1440, 240, 480,
    2100, 2100, 2100, 2100, 1500, 1800
};

void animations_init(void)
{
    memset(tower_animations, 0, sizeof tower_animations);
    memset(shot_styles, V_DART, sizeof shot_styles);
    for (unsigned i = 0; i < TOWER_LIMIT; i++)
        tower_animations[i].facing = 2;
    animation_count = 0;
    replace = 0;
    animations_tick();
}

void animations_tick(void)
{
    memset(pop_tiles, 0, sizeof pop_tiles);
    pop_count = event_count = 0;

    for (unsigned i = 0; i < animation_count;) {
        AnimationEffect *effect = &animation_effects[i];
        if (game.time - effect->born >= lifetimes[effect->kind]) {
            animation_effects[i] = animation_effects[--animation_count];
        } else {
            i++;
        }
    }
}

unsigned animation_direction(int32_t x, int32_t y)
{
    uint32_t ax = x < 0 ? -(uint32_t)x : (uint32_t)x;
    uint32_t ay = y < 0 ? -(uint32_t)y : (uint32_t)y;
    if (ax > ay * 2)
        return x < 0 ? 4 : 0;
    if (ay > ax * 2)
        return y < 0 ? 6 : 2;
    if (x < 0)
        return y < 0 ? 5 : 3;
    return y < 0 ? 7 : 1;
}

static void effect(unsigned kind, int32_t x, int32_t y, int32_t end_x, int32_t end_y)
{
    // Cosmetic bursts have a fixed budget independent of gameplay projectile capacity.
    if (event_count++ >= 18)
        return;
    unsigned slot = animation_count < EFFECT_LIMIT ? animation_count++ : replace++ % EFFECT_LIMIT;
    animation_effects[slot] = (AnimationEffect){x, y, end_x, end_y, game.time, kind};
}

void animation_pop(int32_t x, int32_t y)
{
    if (pop_count >= 6)
        return;
    unsigned tile = (((uint32_t)x >> 12) * 37 + ((uint32_t)y >> 12)) & 255;
    uint32_t bit = 1u << (tile & 31);
    if (pop_tiles[tile >> 5] & bit)
        return;
    pop_tiles[tile >> 5] |= bit;
    pop_count++;
    effect(FX_POP, x, y, x, y);
}

void animation_line(unsigned kind, int32_t x, int32_t y, int32_t end_x, int32_t end_y)
{
    effect(kind, x, y, end_x, end_y);
}

unsigned animation_frame(const AnimationEffect *effect, unsigned frames)
{
    unsigned frame = (game.time - effect->born) * frames / lifetimes[effect->kind];
    return frame < frames ? frame : frames - 1;
}

void animation_fired(unsigned owner, int32_t x, int32_t y, int target)
{
    TowerAnimation *animation = &tower_animations[owner];
    animation->fired = game.time;
    animation->firing = 1;
    if (target >= 0) {
        Bloon *bloon = &game.bloons[target];
        animation->facing = animation_direction(bloon->x - x, bloon->y - y);
    }
}

void animation_area(unsigned owner, const AttackDef *attack, int32_t x, int32_t y)
{
    unsigned kind = FX_EXPLOSION;
    unsigned type = owner < TOWER_LIMIT ? game.towers[owner].type : 0;

    if (attack->flags & (A_GLUE | A_FOAM | A_WALL))
        return;
    if (type == 15 && attack->trigger == TR_CONTACT && attack->refresh_pierce)
        return;

    if (attack->flags & A_FREEZE)
        kind = FX_FREEZE;
    else if (!attack_has_damage(attack) && (attack->flags & A_DECAMO))
        kind = FX_SHIMMER;
    else if (type == 3)
        kind = FX_RING_FIRE;
    else if (attack->flags & A_WIND)
        kind = FX_WIND;
    else if (type == 14) {
        if (attack->trigger == TR_POP)
            kind = FX_EXPLOSION_XL;
        else if (attack->flags & A_CONCOCTION)
            kind = FX_CONCOCTION;
        else {
            kind = FX_ACID;
            const TowerDef *definition = tower_defs[type];
            for (unsigned p = 0; p < definition->profile_count; p++) {
                const TowerProfile *source = &definition->profiles[p];
                for (unsigned a = 0; a < source->attack_count; a++) {
                    const AttackDef *parent = &source->attacks[a];
                    for (unsigned c = 0; c < parent->child_count; c++) {
                        if (attack != &parent->children[c])
                            continue;
                        kind = source->tiers[1] >= 1 ? FX_STRONG_ACID
                                : source->tiers[0] >= 1 ? FX_LARGE_ACID : FX_ACID;
                        goto splash_found;
                    }
                }
            }
        splash_found:;
        }
    } else if (type == 11)
        kind = FX_FIREBALL;

    effect(kind, x, y, x, y);
}

unsigned animation_attack_style(unsigned owner, const AttackDef *attack)
{
    if (owner >= TOWER_LIMIT)
        return V_DART;

    const Tower *tower = &game.towers[owner];
    const TowerProfile *profile = tower_profile(tower);
    if (tower->type < 20) {
        const TowerDef *definition = tower_defs[tower->type];
        // Emitted attacks retain their original artwork when the tower upgrades.
        uintptr_t base = (uintptr_t)&definition->profiles[0].attacks[0];
        uintptr_t address = (uintptr_t)attack;
        if (address >= base) {
            uintptr_t offset = address - base;
            unsigned source_profile = offset / sizeof(TowerProfile);
            unsigned remainder = offset % sizeof(TowerProfile);
            unsigned source_attack = remainder / sizeof(AttackDef);
            unsigned first = combat_profile_offsets[tower->type];
            unsigned end = combat_profile_offsets[tower->type + 1];
            if (source_profile < end - first && remainder % sizeof(AttackDef) == 0 &&
                source_attack < definition->profiles[source_profile].attack_count)
                return combat_profile_styles[first + source_profile][source_attack];
        }
    }

    if (attack->motion_flags & MF_PATH_REVERSE)
        return V_ZOMBIE;
    if (attack->flags & A_WALL)
        return V_WALL;
    if (attack->flags & A_FOAM)
        return V_FOAM;
    if (attack->flags & A_SPIKE)
        return V_SPIKE;
    if (tower->type == 15 && attack->trigger == TR_CONTACT && attack->refresh_pierce)
        return attack->immunity & IMM_LEAD ? V_VINE : V_HOT_VINE;
    if (attack->flags & A_AREA)
        return V_NONE;
    if ((attack->flags & A_CHAIN) && attack->chain_splits)
        return V_LIGHTNING_BALL;
    if (attack->flags & A_WIND)
        return V_WIND;
    if (attack->flags & A_GLUE)
        return V_GLUE;
    if (attack->flags & A_MORTAR)
        return V_NONE;

    switch (tower->type) {
    case 1:
        if (profile && profile->tiers[0] >= 2)
            return profile->tiers[2] >= 2 ? V_HOT_GLAIVE : V_GLAIVE;
        return V_BOOMERANG;
    case 2: return V_BOMB;
    case 3: return V_TACK;
    case 4: return V_ICE;
    case 6: return V_SHRAPNEL;
    case 7:
    case 8: return attack->flags & A_EXPLOSION ? V_ROCKET : V_DART;
    case 10: return attack->flags & A_EXPLOSION ? V_HYDRA_ROCKET : V_DARTLING_DART;
    case 11: return attack->flags & A_EXPLOSION ? V_FIRE : V_MAGIC;
    case 12:
        return profile && profile->tiers[0] >= 2 ? V_PLASMA
                : profile && profile->tiers[0] >= 1 ? V_LASER : V_DART;
    case 13: return attack->flags & A_HOMING ? V_SEEKING_SHURIKEN : V_SHURIKEN;
    case 14: return V_POTION;
    case 15: return V_THORN;
    case 19: return (attack->flags & A_DECAMO) && (attack->flags & A_DEGROW)
                        ? V_NAIL_FOAM : V_NAIL;
    }
    return V_DART;
}

unsigned animation_glue_art(unsigned owner, const AttackDef *attack)
{
    unsigned style = animation_attack_style(owner, attack);
    return style == V_GLUE_GREEN ? 2 : style == V_GLUE_PINK ? 3 : 1;
}

void animation_shot(unsigned id)
{
    const Shot *shot = &game.shots[id];
    shot_styles[id] = animation_attack_style(shot->owner, shot->attack);
}
