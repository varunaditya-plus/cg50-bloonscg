#ifndef BLOONSCG_GAME_H
#define BLOONSCG_GAME_H
#include <stdint.h>
#include <stddef.h>

// Q8 world distances; 6000 time units per second, updated at 50 Hz.
enum {
    TICK = 120,
    Q = 256,
    TOWER_LIMIT = 256,
    BLOON_LIMIT = 4096,
    SHOT_LIMIT = 16384,
    HISTORY_LIMIT = 64,
    SHOT_HISTORY_BLOCKS = 4096,
    ATTACK_LIMIT = 6,
    MAP_W = 384,
    MAP_H = 216,
    MONKEY_COUNT = 20,
    SENTRY_LIMIT = 4096,
    CASH_DROP_LIMIT = 4096
};
enum {
    RED,
    BLUE,
    GREEN,
    YELLOW,
    PINK,
    BLACK,
    WHITE,
    PURPLE,
    LEAD,
    ZEBRA,
    RAINBOW,
    CERAMIC,
    MOAB,
    BFB,
    BLOON_TYPES
};
enum { CAMO = 1, REGROW = 2, FORTIFIED = 4 };
enum { IMM_LEAD = 1, IMM_BLACK = 2, IMM_WHITE = 4, IMM_PURPLE = 8, IMM_FROZEN = 16 };
enum { MF_PATH_REVERSE = 4096, NECRO_CHOOSE = 65535, NECRO_NONE = 65534 };
enum attack_flags {
    A_RADIAL = 1u << 0,
    A_AREA = 1u << 1,
    A_HITSCAN = 1u << 2,
    A_EXPLOSION = 1u << 3,
    A_HOMING = 1u << 4,
    A_CHAIN = 1u << 5,
    A_BOUNCE = 1u << 6,
    A_RETURN = 1u << 7,
    A_FREEZE = 1u << 8,
    A_GLUE = 1u << 9,
    A_STUN = 1u << 10,
    A_PUSH = 1u << 11,
    A_DOT = 1u << 12,
    A_DECAMO = 1u << 13,
    A_DEGROW = 1u << 14,
    A_DEFORT = 1u << 15,
    A_VULNERABLE = 1u << 16,
    A_SHRAPNEL = 1u << 17,
    A_MOAB_ONLY = 1u << 18,
    A_NORMAL_ONLY = 1u << 19,
    A_SPIKE = 1u << 20,
    A_MORTAR = 1u << 21,
    A_BEAM = 1u << 22,
    A_WALL = 1u << 23,
    A_VINE = 1u << 24,
    A_CONCOCTION = 1u << 25,
    A_FOAM = 1u << 26,
    A_TRAP = 1u << 27,
    A_ROTOR = 1u << 28,
    A_WIND = 1u << 29,
    A_ACID = 1u << 30,
    A_SENTRY = 1u << 31
};
enum support_flags {
    S_VILLAGE = 1,
    S_ALCHEMIST = 2,
    S_SHINOBI = 4,
    S_FARM = 8,
    S_SENTRY = 16,
    S_WRATH = 32,
    S_AIR = 64
};

enum attack_trigger {
    TR_MAIN,
    TR_CONTACT,
    TR_EXHAUST,
    TR_PIERCE,
    TR_DAMAGE,
    TR_POP,
    TR_INTERVAL,
    TR_BEAM_END,
    TR_BLOCKER
};
enum attack_target_flags {
    AT_GLOBAL = 1,
    AT_THROUGH_WALLS = 2,
    AT_FIRE_WITHOUT_TARGET = 4,
    AT_NO_FROZEN = 8,
    AT_NO_LEAD = 16,
    AT_NO_MOAB = 32,
    AT_NO_BAD = 64,
    AT_CAMO_PRIORITY = 128,
    AT_NO_GLUE = 256,
    AT_NO_WIND = 512,
    AT_NO_BOSS = 1024,
    AT_NO_CONCOCTION = 2048,
    AT_REQUIRE_CAMO_TARGET = 4096
};
enum effect_kind {
    EF_FREEZE,
    EF_SLOW,
    EF_DOT,
    EF_VULNERABLE,
    EF_REMOVE_DAMAGE_TYPE,
    EF_CLEANSE,
    EF_REMOVE_GLUE_ICE,
    EF_PUSH,
    EF_KNOCKBACK,
    EF_WIND,
    EF_MAIM,
    EF_SHOCK_BONUS,
    EF_SHARDS,
    EF_CONCOCTION,
    EF_GLUE_LEVEL,
    EF_PIERCE_COST,
    EF_REGEN_BLOCK,
    EF_SLOW_MODIFIER,
    EF_FREEZE_MODIFIER,
    EF_DOT_MODIFIER,
    EF_DAMAGE_MODIFIER,
    EF_ACID_POOL,
    EF_CONCOCTION_SPLASH
};
enum model_tag {
    TAG_MOABS = 1u << 0,
    TAG_CERAMIC = 1u << 1,
    TAG_FORTIFIED = 1u << 2,
    TAG_CAMO = 1u << 3,
    TAG_LEAD = 1u << 4,
    TAG_BLACK = 1u << 5,
    TAG_WHITE = 1u << 6,
    TAG_ZEBRA = 1u << 7,
    TAG_MOAB = 1u << 8,
    TAG_BFB = 1u << 9,
    TAG_ZOMG = 1u << 10,
    TAG_DDT = 1u << 11,
    TAG_BAD = 1u << 12,
    TAG_BOSS = 1u << 13,
    TAG_MINIBOSS = 1u << 14
};
enum mutation_kind {
    MUT_NONE,
    MUT_ICE,
    MUT_GLUE,
    MUT_ACID,
    MUT_CORROSIVE,
    MUT_DISSOLVER,
    MUT_BURN,
    MUT_LASER_SHOCK,
    MUT_BRITTLE,
    MUT_UNSTABLE,
    MUT_WIND,
    MUT_STUN
};
typedef struct {
    int32_t speed, rotation, pursuit_distance;
    int32_t circle_radius;
    int32_t force_start, force_end, brake_force, repel_force, repel_radius;
    int32_t slowdown_max, slowdown_min, min_velocity;
    uint32_t takeoff_time;
    uint16_t takeoff_exponent, catchup_speed;
} AirMovement;
enum effect_flags {
    EF_UNIQUE = 1,
    EF_CASCADE = 2,
    EF_AFTER_DAMAGE = 4,
    EF_ONLY_DAMAGED = 8,
    EF_NO_REFRESH = 16,
    EF_IMMEDIATE = 32,
    EF_PERSIST_DEGRADE = 64,
    EF_PREVENT_MOAB = 128
};
typedef struct {
    int32_t x, y;
} AttackCurvePoint;
typedef struct {
    uint16_t source_tag, bloon_damage, moab_damage;
} AttackDamageSource;
typedef struct {
    uint16_t kind, flags, layers, damage, immunity, multiplier;
    uint32_t time, period;
    int32_t value, auxiliary[4];
    uint16_t mutation, tags, exclude_tags, chance;
    uint16_t modifier_flags;
} AttackEffect;

typedef struct AttackDef {
    uint32_t flags;
    uint32_t period, life, status_time, dot_period;
    int32_t speed;
    uint16_t range, radius, area, damage, pierce, ceramic, moab, fortified;
    uint16_t immunity, count, spread, slow, moab_slow, dot_damage;
    uint16_t status_layers, crit_every, crit_damage, chain_range;
    uint16_t push, hit_reset, vulnerability;
    uint16_t moab_stun, bfb_stun;
    uint8_t status_cascade;
    uint8_t camo;
    uint16_t target_flags, camo_damage, chain_delay, random_spread;
    uint16_t trigger, child_count, effect_count, curve_count, ease_count;
    int32_t homing_turn, homing_acceleration, homing_max_speed;
    int32_t homing_min_speed, homing_max_turn;
    int32_t homing_turn_acceleration;
    uint16_t homing_kind, homing_range, homing_seek_angle;
    uint16_t homing_accelerate_angle, homing_decelerate_angle, homing_flags;
    uint16_t fixed_target;
    uint16_t inner_radius;
    uint32_t curve_time, curve_length, interval, refresh_pierce, collision_interval;
    uint16_t curve_kind, motion_flags, chain_splits;
    uint16_t expire_rounds, target_kind;
    uint16_t collect_radius, collide_tags;
    uint16_t target_spread;
    int16_t emission_start, emission_span, emission_y;
    uint16_t emission_kind;
    uint16_t target_track_offset;
    uint16_t life_pierce_per, life_pierce_cap;
    uint16_t source_damage_count, default_bloon_damage, default_moab_damage;
    uint16_t trap_capacity, trap_cash_multiplier, projectile_limit;
    uint32_t trap_close_delay, projectile_limit_delay;
    const AttackDamageSource *source_damage;
    const struct AttackDef *children;
    const AttackEffect *effects;
    const AttackCurvePoint *curve, *ease;
    const uint32_t *curve_distances;
} AttackDef;
static inline int attack_has_damage(const AttackDef *a)
{
    return a->damage || (a->motion_flags & 2048);
}

typedef struct TowerProfile {
    uint8_t tiers[3], attack_count;
    uint16_t range, footprint, support, income, income_count;
    uint16_t buff_damage, buff_pierce, buff_speed;
    uint32_t buff_time;
    uint16_t footprint_x, footprint_y;
    uint16_t buff_shots, support_stacks, support_max_stacks;
    uint16_t support_camo, support_immunity;
    uint8_t footprint_rectangle, air_pursuit;
    uint32_t buff_period, buff_block_time, acid_period, sub_period, sub_life;
    uint16_t acid_shots, acid_cap, buff_range_multiplier;
    uint16_t wrath_threshold, wrath_increment, wrath_max_stacks, wrath_timeout;
    uint16_t life_speed_per, life_speed_cap, life_speed_base;
    uint16_t regrow_block_range;
    const struct TowerProfile *sub_profiles;
    uint16_t sub_profile_count;
    const AirMovement *air_movement;
    uint32_t income_period, income_life;
    uint16_t income_radius, income_min_radius, income_max_radius;
    uint16_t buff_throw_speed, sub_min_radius, sub_max_radius;
    uint16_t sub_placement_radius;
    uint16_t round_start_rate;
    uint32_t round_start_time;
    AttackDef attacks[ATTACK_LIMIT];
} TowerProfile;
typedef struct {
    const char *name;
    uint16_t price, icon;
} Upgrade;
typedef struct {
    const char *name;
    uint16_t price;
    uint8_t caps[3], profile_count;
    const TowerProfile *profiles;
    const Upgrade *upgrades[3];
} TowerDef;
enum { DOT_LIMIT = 8 };
typedef struct {
    uint32_t time, clock, period;
    uint16_t damage, immunity, owner, layers, family;
    uint16_t moab_damage, ceramic_damage, fortified_damage, camo_damage;
    uint8_t flags;
    uint8_t mutation : 4, fortified_lead_damage : 4;
} DotStatus;
typedef struct {
    int32_t x, y, distance;
    int32_t wind_remaining;
    uint16_t wind_multiplier;
    uint32_t status, freeze, stun, glue, regrow_clock;
    uint32_t vulnerability;
    uint16_t generation, hp, slow, moab_slow;
    uint8_t type, flags, active, regrow_max, vulnerable_damage;
    uint16_t freeze_layers, glue_layers, stun_layers;
    uint16_t ice_slow, ice_slow_layers;
    int16_t move_remainder;
    uint16_t pop_source, knockback_multiplier;
    uint32_t knockback;
    uint16_t property_strip, strip_layers, glue_level;
    uint32_t strip_time, ice_slow_time;
    uint16_t regrow_paid_layers;
    uint16_t regrow_branch;
    uint8_t cash_disabled, ancestor_count;
    uint32_t ancestors[16];
    const AttackDef *pop_attack;
    DotStatus dots[DOT_LIMIT];
} Bloon;
typedef struct {
    int32_t x, y, aim_x, aim_y, air_x, air_y;
    int32_t air_vx, air_vy;
    uint32_t air_phase, air_angle, air_age;
    int16_t air_remainder_x, air_remainder_y;
    uint8_t air_on_path;
    uint32_t clocks[ATTACK_LIMIT], buff_clock, wrath;
    uint32_t spent, pops;
    uint16_t shots[ATTACK_LIMIT], buff_shots;
    uint8_t type, profile, active;
    uint16_t alchemist;
    uint32_t acid_clock, brew_clock, acid_flight, brew_flight, brew_block;
    const TowerProfile *acid_profile, *brew_profile;
    uint32_t wrath_pops, wrath_idle, support_clock;
    uint16_t acid_target, brew_target, acid_shots;
    uint16_t brew_damage, brew_pierce, brew_rate, brew_range;
    uint16_t support_rate, support_range, support_pierce, support_immunity;
    uint16_t pierce_remainder[ATTACK_LIMIT];
    uint32_t farm_round;
    uint32_t necro_round, necro_clock, necro_budget_clock;
    uint16_t necro_grave[2], necro_used;
    uint16_t placement_lives, lowest_lives, farm_emitted;
    uint16_t previous_lives, lives_gained;
    uint8_t support_camo, support_initialised;
} Tower;
typedef struct {
    int32_t x, y, vx, vy, origin_x, origin_y;
    uint32_t life, age, hit_clock;
    const AttackDef *attack;
    uint32_t history_first;
    uint32_t chain_clock;
    int32_t dir_x, dir_y;
    int32_t destination_x, destination_y;
    uint32_t curve_time;
    uint32_t born_tick;
    uint32_t pierce_clock;
    uint32_t collision_clock;
    uint32_t child_clock;
    int32_t fork_x[2], fork_y[2];
    uint32_t trap_cash, trap_close;
    uint16_t owner, pierce, damage, target, history_count;
    uint16_t history_block, active_index;
    uint16_t trap_used;
    uint16_t immunity, ceramic_bonus, moab_bonus, fortified_lead_bonus;
    uint16_t damage_bonus, pierce_bonus, pierce_factor, brew_pierce;
    uint16_t immunity_removed;
    uint8_t active, fork_count, trap_full, camo;
} Shot;
typedef struct {
    uint32_t tokens[HISTORY_LIMIT];
    uint16_t next;
    uint8_t count;
} ShotHistoryBlock;
typedef struct {
    int32_t x, y;
    uint32_t clock, life, flight;
    const TowerProfile *profile;
    uint16_t owner;
    uint8_t active;
} Sentry;
typedef struct {
    int32_t x, y;
    uint32_t life;
    uint16_t amount;
    uint8_t active;
} CashDrop;
typedef struct {
    uint8_t child[2], count[2];
    uint16_t hp, fort_hp, radius, speed, leak, fort_leak, immunity;
} BloonDef;
typedef struct {
    uint8_t type, flags;
    uint16_t count;
    uint32_t start, end;
} RoundGroup;
typedef struct {
    uint16_t first, count;
    uint32_t end;
} RoundDef;
typedef struct {
    int32_t x, y;
    uint32_t distance, reciprocal;
} PathPoint;
typedef struct {
    Tower towers[TOWER_LIMIT];
    Bloon bloons[BLOON_LIMIT];
    Shot shots[SHOT_LIMIT];
    Sentry sentries[SENTRY_LIMIT];
    CashDrop cash_drops[CASH_DROP_LIMIT];
    uint16_t shot_active[SHOT_LIMIT], shot_free[SHOT_LIMIT];
    ShotHistoryBlock shot_history[SHOT_HISTORY_BLOCKS];
    uint16_t shot_history_free[SHOT_HISTORY_BLOCKS];
    uint16_t shot_free_count, shot_history_free_count;
    uint32_t support_random;
    uint16_t sentry_count, cash_drop_count;
    uint32_t time, round_time, cash, round;
    uint16_t lives, tower_count, bloon_count, shot_count;
    uint16_t group_spawned[64];
    uint8_t running, won, lost, speed, pool_full;
} Game;
#ifdef __sh__
// https://www.planet-casio.com/Fr/forums/topic18535-1-gint-programming-questions.html
#define game (*(Game *)0x8c200000)
_Static_assert(sizeof(Game) < 11 * 1024 * 1024 / 2, "Game exceeds reserved CG50 RAM");
#else
extern Game game;
#endif
extern const TowerDef *tower_defs[MONKEY_COUNT];
extern const BloonDef bloon_defs[BLOON_TYPES];
extern const RoundGroup round_groups[];
extern const RoundDef rounds[60];
extern const PathPoint meadow_path[];
extern const unsigned meadow_path_count;
extern const uint32_t meadow_path_length;

int32_t distance_squared(int32_t x, int32_t y, int32_t a, int32_t b);
uint32_t integer_sqrt(uint32_t value);
int world_x(int screen_x);
int world_y(int screen_y);
int screen_x(int world);
int screen_y(int world);
void path_position(int32_t distance, int32_t *x, int32_t *y);
uint32_t game_random(void);
void game_init(void);
void game_tick(void);
void game_start_round(void);
const RoundDef *game_round(void);
int can_place_monkey(unsigned type, int x, int y, int ignore);
int game_place(unsigned type, int x, int y);
int game_upgrade(unsigned tower, unsigned path);
int game_upgrade_allowed(unsigned tower, unsigned path);
const TowerProfile *tower_profile(const Tower *tower);
const Upgrade *tower_next_upgrade(const Tower *tower, unsigned path);
void game_sell(unsigned tower);
int bloon_spawn(unsigned type, unsigned flags, int32_t distance, unsigned max_regrow);
int bloon_next(unsigned start);
void bloons_reset_cache(void);
void bloon_remove(Bloon *bloon);
void bloons_tick(void);
void rounds_tick(void);
int bloon_damage(unsigned id, unsigned damage, unsigned immunity, unsigned owner);
void bloon_effect_buffed(unsigned id, const AttackDef *attack, unsigned owner, unsigned immunity,
                         const Shot *snapshot);
void bloon_clear_glue_ice(Bloon *bloon);
void towers_tick(void);
void shots_tick(void);
int shot_acquire(void);
void shot_release(Shot *shot);
void shot_history_reset(Shot *shot);
int shot_history_contains(const Shot *shot, unsigned enemy);
int shot_history_record(Shot *shot, unsigned enemy);
void shots_init(void);
void shots_owner_destroy(unsigned owner);
void combat_reset_cache(void);
int fixed_sine(int degrees);
int projectile_setup(Shot *shot, const AttackDef *attack, unsigned owner, int target);
int projectile_step(Shot *shot, int32_t *next_x, int32_t *next_y);
int projectile_keep_on_zero(const AttackDef *attack);
int projectile_can_hit(const Shot *shot, unsigned enemy);
unsigned projectile_pierce_cost(const AttackDef *attack, unsigned enemy);
void projectile_round_end(void);
int projectile_child_ready(Shot *shot, const AttackDef *child);
void projectile_emission(Shot *shot, const AttackDef *attack, unsigned index);
int projectile_wind_apply(Bloon *bloon, const AttackEffect *effect, unsigned immunity);
int projectile_wind_step(Bloon *bloon);
int lightning_step(unsigned shot);
int combat_eligible(unsigned owner, const AttackDef *attack, unsigned enemy);
int32_t collision_contact_fraction(const Shot *shot, int32_t next_x, int32_t next_y,
                                   const Bloon *bloon, unsigned radius);
int collision_area_contains(const AttackDef *attack, int32_t x, int32_t y, const Bloon *bloon);
void necromancy_reset_cache(void);
void necromancy_begin_tick(void);
unsigned necromancy_pop(const Bloon *bloon, unsigned owner, unsigned recipient);
void necromancy_tick(unsigned owner, const TowerProfile *profile);
int necromancy_step(Shot *shot, int32_t *next_x, int32_t *next_y);
unsigned necromancy_damage(unsigned owner);
void air_tick(Tower *tower, const TowerProfile *profile);
int tower_target(const Tower *tower, const AttackDef *attack, int32_t x, int32_t y);
void attack_emit(unsigned owner, const AttackDef *attack, int32_t x, int32_t y, int target);
void shot_hit(unsigned shot, unsigned bloon);
void combat_effect(unsigned bloon, const AttackDef *attack, unsigned owner);
typedef struct CombatPop CombatPop;
CombatPop *combat_pop_begin(const Bloon *bloon);
int combat_pop_next(CombatPop *pop, unsigned *enemy, unsigned *damage, unsigned *immunity);
void combat_pop_finish(CombatPop *pop);
void support_begin_tick(void);
void support_tick(unsigned owner, const TowerProfile *profile);
void support_units_tick(void);
void support_attack(unsigned owner, const AttackDef *raw, AttackDef *effective, unsigned *speed);
void support_shot(unsigned owner, const AttackDef *raw, Shot *shot);
void support_fired(unsigned owner);
unsigned support_collect(int32_t x, int32_t y, unsigned radius);
int support_blocks_regrow(const Bloon *bloon);
void support_round_end(void);
int trap_emit_allowed(unsigned owner, const AttackDef *attack);
int trap_capture(Shot *shot, unsigned bloon);
void trap_tick(Shot *shot);
unsigned trap_collect(int32_t x, int32_t y);
void ui_init(void);
void ui_draw(void);
int ui_key(int key);
int ui_paused(void);
#endif
