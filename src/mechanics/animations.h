#ifndef BLOONSCG_ANIMATIONS_H
#define BLOONSCG_ANIMATIONS_H

#include "game.h"

enum {
    V_DART, V_NAIL, V_BULLET, V_BOOMERANG, V_GLAIVE, V_SHURIKEN, V_BOMB, V_ROCKET,
    V_GLUE, V_MAGIC, V_FIRE, V_LASER, V_PLASMA, V_THORN, V_ICE, V_SPIKE, V_POTION,
    V_LIGHTNING_BALL, V_SEEKING_SHURIKEN, V_HOT_GLAIVE, V_ZOMBIE, V_WALL, V_FOAM,
    V_WIND, V_NONE, V_ARROW, V_MASTER_ARROW, V_SPIKE_O_PULT, V_JUGGERNAUT,
    V_RED_HOT_RANG, V_KYLIE, V_MOAB_PRESS, V_KYLIE_GLAIVE, V_MOAB_MAULER,
    V_BIG_BOMB, V_HEAVY_MISSILE, V_REALLY_BIG_BOMB, V_CANNON_MISSILE, V_TACK,
    V_HOT_TACK, V_FREEZE_BOMB, V_GLUE_PINK, V_GLUE_GREEN, V_GREEN_DART,
    V_ACE_DART, V_FIGHTER_MISSILE, V_HELI_DART, V_DARTLING_DART, V_HYDRA_ROCKET,
    V_SHOCK_DART, V_SHOCK_ROCKET, V_LASER_CANNON, V_DRAGONS_BREATH, V_ARCANE_BLAST,
    V_INTENSE_MAGIC, V_SHIMMER, V_INTENSE_BLAST, V_DARK_RANG, V_DARK_LASER,
    V_DARK_PLASMA, V_SUN_BLAST, V_SHARP_SHURIKEN, V_STRONG_POTION,
    V_PERISHING_POTION, V_CONCOCTION_POTION, V_LARGE_POTION, V_LARGE_PURPLE_POTION,
    V_LARGE_PERISHING_POTION, V_GREEN_THORN, V_PURPLE_THORN, V_PURPLE_GREEN_THORN,
    V_OVERSIZED_NAIL, V_FOAM_SHOT, V_HOT_SPIKE, V_SPIKED_BALL, V_SHRAPNEL,
    V_NAIL_FOAM, V_HOT_VINE, V_VINE
};
enum {
    FX_POP, FX_EXPLOSION, FX_FIRE, FX_FREEZE, FX_SHIMMER, FX_SPARK, FX_RING_FIRE,
    FX_WIND, FX_BEAM, FX_LIGHTNING, FX_ACID, FX_STRONG_ACID, FX_LARGE_ACID,
    FX_CONCOCTION, FX_FIREBALL, FX_EXPLOSION_XL, FX_COUNT, EFFECT_LIMIT = 64
};

typedef struct {
    int32_t x, y, end_x, end_y;
    uint32_t born;
    uint8_t kind;
} AnimationEffect;

typedef struct {
    uint32_t fired;
    uint8_t facing, firing;
} TowerAnimation;

extern AnimationEffect animation_effects[EFFECT_LIMIT];
extern TowerAnimation tower_animations[TOWER_LIMIT];
extern uint8_t shot_styles[SHOT_LIMIT];
extern unsigned animation_count;

void animations_init(void);
void animations_tick(void);
void animation_shot(unsigned id);
unsigned animation_attack_style(unsigned owner, const AttackDef *attack);
unsigned animation_glue_art(unsigned owner, const AttackDef *attack);
void animation_fired(unsigned owner, int32_t x, int32_t y, int target);
void animation_pop(int32_t x, int32_t y);
void animation_area(unsigned owner, const AttackDef *attack, int32_t x, int32_t y);
void animation_line(unsigned kind, int32_t x, int32_t y, int32_t end_x, int32_t end_y);
unsigned animation_direction(int32_t x, int32_t y);
unsigned animation_frame(const AnimationEffect *effect, unsigned frames);

#endif
