#include "game.h"
#include "animations.h"
#include "combat_art.h"
#include "world_art.h"
#include "support_art.h"
#include "monkeys/appearance.h"
#include <gint/display.h>
#include <gint/keyboard.h>
#include <stdio.h>
#include <stdlib.h>

// Native meshes, portraits and icons: https://store.steampowered.com/app/960090/Bloons_TD_6/
extern const bopti_image_t img_meadow, img_monkey_portraits, img_monkey_sprites;
extern const bopti_image_t img_bloons, img_upgrade_icons, img_ui_hud;
extern const bopti_image_t img_upgrade_portraits;
extern const bopti_image_t img_combat_sprites, img_tower_facing;
extern const font_t font_game_ui, font_game_title;
extern const bopti_image_t img_support_sprites;
extern uint8_t bloon_glue_art[BLOON_LIMIT], sentry_facing[SENTRY_LIMIT];

enum { SELECT, PLACE, PICK, MODAL };
enum { COLLECT = 3, SELL };
enum { MODAL_X = 8, MODAL_Y = 22, MODAL_WIDTH = 308, MODAL_HEIGHT = 180 };
enum { MODAL_RIGHT = MODAL_X + MODAL_WIDTH, MODAL_BOTTOM = MODAL_Y + MODAL_HEIGHT };

typedef struct {
    int mode;
    int selected;
    int first_row;
    int x;
    int y;
    int tower;
    int row;
} UI;

UI ui;
static uint16_t red_palette[256];
static bopti_image_t red_sprites;
static uint32_t map_dirty[13];
static uint16_t *render_vram;
static uint32_t selector_key, footer_key;
static unsigned hud_cash, hud_lives, hud_round;
static int previous_mode;
static int exit_confirmation;
static struct {
    int valid, tower, type, profile, row;
    uint32_t pops, spent;
    unsigned affordable;
} modal_cache;

#define UI_RGB(hex) (((hex >> 19) & 31) << 11 | ((hex >> 10) & 63) << 5 | ((hex >> 3) & 31))
enum {
    WOOD = UI_RGB(0xb38d57),
    TAN = UI_RGB(0xcfab73),
    CREAM = UI_RGB(0xfff1c7),
    BROWN = UI_RGB(0x613917),
    BLUE_UI = UI_RGB(0x168eb8),
    SKY = UI_RGB(0x6bd8f4),
    BLUE_EDGE = UI_RGB(0x075375),
    GREEN_UI = UI_RGB(0x48b90a),
    LIME = UI_RGB(0x62df12),
    GREEN_EDGE = UI_RGB(0x245d06),
    ORANGE = UI_RGB(0xef7909),
    ORANGE_EDGE = UI_RGB(0xa64209),
    GOLD = UI_RGB(0xffdb57),
    DISABLED = UI_RGB(0x899baa),
    DISABLED_EDGE = UI_RGB(0x435360),
    RED_UI = UI_RGB(0xc52f22)
};

void ui_init(void)
{
    ui = (UI){0};
    render_vram = NULL;
    selector_key = footer_key = UINT32_MAX;
    hud_cash = hud_lives = hud_round = UINT32_MAX;
    previous_mode = SELECT;
    exit_confirmation = 0;
    modal_cache.valid = 0;
    red_sprites = img_monkey_sprites;
    red_sprites.palette = red_palette;

    for (int i = 0; i < img_monkey_sprites.color_count; i++) {
        uint16_t c = img_monkey_sprites.palette[i];
        red_palette[i] =
            ((((c >> 11) + 93) / 4) << 11) | ((((c >> 5) & 63) / 4) << 5) | ((c & 31) / 4);
    }
}

static void map_mark(int x, int y, int width, int height)
{
    int right = x + width - 1;
    int bottom = y + height - 1;

    if (right < 0 || bottom < 0 || x >= 326 || y >= 205) {
        return;
    }
    if (ui.mode == MODAL && x >= MODAL_X + 2 && right < MODAL_RIGHT - 2 &&
        y >= MODAL_Y && bottom < MODAL_BOTTOM) {
        return;
    }

    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (right > 325) {
        right = 325;
    }
    if (bottom > 204) {
        bottom = 204;
    }

    unsigned left_tile = (unsigned)x >> 4;
    unsigned right_tile = (unsigned)right >> 4;
    uint32_t mask = ((2u << right_tile) - 1) & ~((1u << left_tile) - 1);

    for (int row = y >> 4; row <= bottom >> 4; row++) {
        map_dirty[row] |= mask;
    }
}

static int world_clip(int x, int y, int width, int height, unsigned *part)
{
    if (ui.mode != MODAL) {
        if ((*part)++) {
            return 0;
        }
        return 1;
    }

    int right = x + width;
    int bottom = y + height;
    if (x >= MODAL_X + 2 && right <= MODAL_RIGHT - 2 && y >= MODAL_Y &&
        bottom <= MODAL_BOTTOM) {
        return 0;
    }

    // The opaque modal leaves these map bands and its rounded corner pixels exposed.
    static const struct dwindow exposed[] = {
        {0, 0, 326, MODAL_Y}, {0, MODAL_BOTTOM, 326, 205},
        {0, MODAL_Y, MODAL_X, MODAL_BOTTOM},
        {MODAL_RIGHT, MODAL_Y, 326, MODAL_BOTTOM},
        {MODAL_X, MODAL_Y, MODAL_X + 2, MODAL_Y + 1},
        {MODAL_X, MODAL_Y + 1, MODAL_X + 1, MODAL_Y + 2},
        {MODAL_X, MODAL_BOTTOM - 2, MODAL_X + 1, MODAL_BOTTOM - 1},
        {MODAL_X, MODAL_BOTTOM - 1, MODAL_X + 2, MODAL_BOTTOM},
        {MODAL_RIGHT - 2, MODAL_Y, MODAL_RIGHT, MODAL_Y + 1},
        {MODAL_RIGHT - 1, MODAL_Y + 1, MODAL_RIGHT, MODAL_Y + 2},
        {MODAL_RIGHT - 1, MODAL_BOTTOM - 2, MODAL_RIGHT, MODAL_BOTTOM - 1},
        {MODAL_RIGHT - 2, MODAL_BOTTOM - 1, MODAL_RIGHT, MODAL_BOTTOM}
    };

    while (*part < sizeof exposed / sizeof exposed[0]) {
        struct dwindow area = exposed[(*part)++];
        if (x < area.right && right > area.left && y < area.bottom && bottom > area.top) {
            dwindow_set(area);
            return 1;
        }
    }
    return 0;
}

// Figma fills the viewport from the original map crop at x=5, scaled by 326/315.
static int map_x(int world)
{
    int n = (screen_x(world) - 5) * 326;
    return (n + (n < 0 ? -157 : 157)) / 315;
}

static int map_y(int world)
{
    int n = screen_y(world) * 326 - 2915;
    return (n + (n < 0 ? -157 : 157)) / 315;
}

static int terrain_x(int x)
{
    return 5 + (x * 315 + 157) / 326;
}

static int terrain_y(int y)
{
    return (y * 315 + 3078) / 326;
}

static const MonkeyAppearance *appearance(const Tower *tower)
{
    return &monkey_appearances[monkey_appearance_offsets[tower->type] + tower->profile];
}

static void monkey(int index, int x, int y, const bopti_image_t *img)
{
    unsigned sprite = monkey_appearances[monkey_appearance_offsets[index]].sprite;
    x += monkey_sprite_anchors[sprite][0];
    y += monkey_sprite_anchors[sprite][1];
    unsigned part = 0;
    while (world_clip(x - 8, y - 8, 16, 16, &part)) {
        dsubimage(x - 8, y - 8, img, index % 2 * 16, index / 2 * 16, 16, 16, DIMAGE_NONE);
    }
    map_mark(x - 8, y - 8, 16, 16);
}

static void combat_sprite(unsigned index, int x, int y)
{
    const SpriteBounds *bounds = &combat_bounds[index];
    if (!bounds->width || !bounds->height)
        return;
    int left = x - 8 + bounds->x;
    int top = y - 8 + bounds->y;
    unsigned part = 0;
    while (world_clip(left, top, bounds->width, bounds->height, &part)) {
        dsubimage(left, top, &img_combat_sprites,
                  index % 16 * 16 + bounds->x, index / 16 * 16 + bounds->y,
                  bounds->width, bounds->height, DIMAGE_NONE);
    }
    map_mark(left, top, bounds->width, bounds->height);
}

static void world_sprite(const bopti_image_t *image, unsigned index, unsigned size,
                         const uint8_t *bounds, int x, int y)
{
    int left = x - size / 2 + bounds[0];
    int top = y - size / 2 + bounds[1];
    unsigned part = 0;

    while (world_clip(left, top, bounds[2], bounds[3], &part)) {
        dsubimage(left, top, image, index % 16 * size + bounds[0],
                  index / 16 * size + bounds[1], bounds[2], bounds[3], DIMAGE_NONE);
    }
    map_mark(left, top, bounds[2], bounds[3]);
}

static unsigned bloon_facing(const Bloon *bloon)
{
    unsigned lo = 0, hi = meadow_path_count - 1;
    while (hi - lo > 1) {
        unsigned mid = (lo + hi) / 2;
        if ((int32_t)meadow_path[mid].distance <= bloon->distance)
            lo = mid;
        else
            hi = mid;
    }

    return animation_direction(meadow_path[lo + 1].x - meadow_path[lo].x,
                               meadow_path[lo + 1].y - meadow_path[lo].y);
}

static unsigned bloon_sprite(unsigned id, unsigned damage, unsigned facing)
{
    const Bloon *bloon = &game.bloons[id];
    if (bloon->type >= MOAB) {
        unsigned status = bloon->glue ? (bloon_glue_art[id] ? bloon_glue_art[id] : 1) : 0;
        return blimp_art[bloon->type - MOAB][!!(bloon->flags & FORTIFIED)][damage][status] + facing;
    }

    return bloon_art[bloon->type][bloon->flags & 7][damage];
}

static void bloon_image(unsigned index, int x, int y)
{
    const BloonSpriteRect *rect = &bloon_rects[index];
    if (!rect->width || !rect->height)
        return;
    int left = x - 12 + rect->x, top = y - 12 + rect->y;
    unsigned part = 0;

    while (world_clip(left, top, rect->width, rect->height, &part)) {
        dsubimage(left, top, &img_bloons, rect->sx, rect->sy,
                  rect->width, rect->height, DIMAGE_NONE);
    }
    map_mark(left, top, rect->width, rect->height);
}

static void bloon_status(unsigned sprite, int x, int y, unsigned *pixels)
{
    const BloonSpriteRect *rect = &bloon_rects[sprite];
    unsigned area = rect->width * rect->height;

    if (area > *pixels)
        return;
    *pixels -= area;
    bloon_image(sprite, x, y);
}

static void draw_bloon(unsigned id, int x, int y, unsigned *status_pixels)
{
    const Bloon *bloon = &game.bloons[id];
    unsigned damage = 0;
    unsigned maximum = bloon->flags & FORTIFIED ? bloon_defs[bloon->type].fort_hp
                                                : bloon_defs[bloon->type].hp;
    while (damage < 4 && bloon->hp * 5u <= maximum * (4u - damage))
        damage++;
    unsigned facing = bloon->type >= MOAB ? bloon_facing(bloon) : 2;
    bloon_image(bloon_sprite(id, damage, facing), x, y);

    // DOT identities and overlay layers: https://github.com/KyleDerZweite/btd6-atlas/tree/ded3155921d70cd83d803b4d70022000e4c37c6b/data/56.3-build-24829026/game-data/Towers
    unsigned acid = 0, burn = 0, shock = 0;
    for (unsigned i = 0; i < DOT_LIMIT; i++) {
        const DotStatus *dot = &bloon->dots[i];
        if (!dot->time)
            continue;
        if (dot->mutation == MUT_ACID)
            acid = dot->period < 12000 ? 2 : 1;
        if (dot->mutation == MUT_LASER_SHOCK)
            shock = 1;
        if (dot->family == 11133 || dot->family == 9561)
            burn = 1;
    }

    if (bloon->type >= MOAB) {
        const uint16_t (*status)[4][8] =
            blimp_status_art[bloon->type - MOAB][!!(bloon->flags & FORTIFIED)][damage];
        unsigned frame = game.time / 500 & 3;
        if (shock)
            bloon_status(status[1][frame][facing], x, y, status_pixels);
        if (burn)
            bloon_status(status[0][frame][facing], x, y, status_pixels);
        if (bloon->pop_attack) {
            const AttackDef *attack = bloon->pop_attack;
            for (unsigned i = 0; i < attack->effect_count; i++) {
                if (attack->effects[i].kind == EF_CONCOCTION) {
                    bloon_status(status[2][0][facing], x, y, status_pixels);
                    break;
                }
            }
        }
        return;
    }

    if (shock)
        bloon_status(normal_status_art[4][game.time / 500 & 3], x, y, status_pixels);

    const uint16_t *overlays = bloon_overlays[bloon->type][!!(bloon->flags & REGROW)];
    unsigned glue = bloon_glue_art[id] ? bloon_glue_art[id] : 1;
    if (bloon->glue)
        bloon_image(overlays[glue - 1], x, y);
    if (bloon->freeze)
        bloon_image(overlays[3], x, y);

    if (acid == 1)
        bloon_image(overlays[4], x, y);
    if (burn) {
        unsigned group = bloon->type == RED || bloon->type == BLACK || bloon->type == WHITE;
        if (bloon->flags & REGROW)
            group += 2;
        bloon_status(normal_status_art[group][game.time / 250 & 3], x, y, status_pixels);
    }
    if (acid == 2)
        bloon_image(overlays[5], x, y);
}

static void attack_effects(void)
{
    unsigned beam_pixels = 8192;

    for (unsigned i = 0; i < animation_count; i++) {
        const AnimationEffect *effect = &animation_effects[i];
        const CombatSprite *sprite = &effect_art[effect->kind];
        int x = map_x(effect->x), y = map_y(effect->y);

        if (!sprite->count)
            continue;
        if (effect->kind != FX_BEAM && effect->kind != FX_LIGHTNING) {
            combat_sprite(sprite->first + animation_frame(effect, sprite->count), x, y);
            continue;
        }

        int end_x = map_x(effect->end_x), end_y = map_y(effect->end_y);
        int dx = end_x - x, dy = end_y - y;
        unsigned length = (unsigned)(abs(dx) > abs(dy) ? abs(dx) : abs(dy));
        unsigned segments = length / 8 + 1;
        unsigned frame = animation_direction(dx, dy);
        const SpriteBounds *bounds = &combat_bounds[sprite->first + frame];
        unsigned pixels = segments * bounds->width * bounds->height;

        if (pixels <= beam_pixels) {
            beam_pixels -= pixels;
            for (unsigned j = 0; j < segments; j++) {
                int tile_x = x + dx * (int)(2 * j + 1) / (int)(2 * segments);
                int tile_y = y + dy * (int)(2 * j + 1) / (int)(2 * segments);
                combat_sprite(sprite->first + frame, tile_x, tile_y);
            }
            continue;
        }

        int left = (x < end_x ? x : end_x) - 3;
        int top = (y < end_y ? y : end_y) - 3;
        int width = (x < end_x ? end_x - x : x - end_x) + 7;
        int height = (y < end_y ? end_y - y : y - end_y) + 7;
        unsigned part = 0;
        while (world_clip(left, top, width, height, &part)) {
            if (effect->kind == FX_BEAM) {
                dline(x, y - 1, end_x, end_y - 1, 0xfd20);
                dline(x, y + 1, end_x, end_y + 1, 0xfd20);
                dline(x, y, end_x, end_y, C_WHITE);
            } else {
                int bend = ((game.time - effect->born) / TICK & 1) ? 2 : -2;
                int mid_x = (x + end_x) / 2 + bend;
                int mid_y = (y + end_y) / 2 - bend;
                dline(x, y, mid_x, mid_y, 0x07ff);
                dline(mid_x, mid_y, end_x, end_y, C_WHITE);
            }
        }
        map_mark(left, top, width, height);
    }
}

static void tower_sprite(unsigned id, int x, int y, int airborne)
{
    const Tower *tower = &game.towers[id];
    const TowerAnimation *animation = &tower_animations[id];
    const MonkeyAppearance *art = appearance(tower);
    unsigned sprite = airborne ? art->air_sprite : art->sprite;
    // Native rotors spin twice per second; four blades repeat every quarter turn.
    if (airborne && tower->type == 8)
        sprite += (game.time % 750) * 2 / 375;
    unsigned facing = animation->facing;
    if (tower->type == 3 || tower->type == 4 || tower->type == 16 ||
        tower->type == 17 || tower->type == 18 ||
        (!airborne && (tower->type == 7 || tower->type == 8)))
        facing = 2;
    if (airborne && (tower->air_vx || tower->air_vy))
        facing = animation_direction(tower->air_vx, tower->air_vy);

    if (animation->firing && game.time - animation->fired < 360 &&
        (tower->type == 0 || tower->type == 2 || tower->type == 6 ||
         tower->type == 10 || tower->type == 12 || tower->type == 19)) {
        static const int8_t recoil_x[] = {-1, -1, 0, 1, 1, 1, 0, -1};
        static const int8_t recoil_y[] = {0, -1, -1, -1, 0, 1, 1, 1};
        x += recoil_x[facing];
        y += recoil_y[facing];
    }
    const MonkeySpriteRect *rect = &monkey_sprite_rects[sprite * 8 + facing];
    int left = x - 8 + monkey_sprite_anchors[sprite][0] + rect->x;
    int top = y - 8 + monkey_sprite_anchors[sprite][1] + rect->y;
    unsigned part = 0;
    while (world_clip(left, top, rect->width, rect->height, &part)) {
        dsubimage(left, top, &img_tower_facing, rect->sx, rect->sy,
                  rect->width, rect->height, DIMAGE_NONE);
    }
    map_mark(left, top, rect->width, rect->height);
}

static int next_tower(int from, int step)
{
    for (int n = 0; n < TOWER_LIMIT; n++) {
        from = (from + step + TOWER_LIMIT) % TOWER_LIMIT;
        if (game.towers[from].active) {
            return from;
        }
    }
    return -1;
}

// Paired stores keep full-screen drawing responsive; callers stay inside VRAM.
static void fill(int x, int y, int width, int height, int color)
{
    if (width <= 0 || height <= 0) {
        return;
    }

    uint32_t pair = (uint16_t)color;
    pair |= pair << 16;
    int right = x + width - 1;
    int left = x + (x & 1);
    int pairs = (((right + 1) & ~1) - left) / 2;

    for (int row = y; row < y + height; row++) {
        uint16_t *line = gint_vram + row * DWIDTH;
        line[x] = color;
        line[right] = color;
        uint32_t *p = (uint32_t *)(line + left);
        int count = pairs;
        while (count >= 8) {
            p[0] = pair;
            p[1] = pair;
            p[2] = pair;
            p[3] = pair;
            p[4] = pair;
            p[5] = pair;
            p[6] = pair;
            p[7] = pair;
            p += 8;
            count -= 8;
        }
        while (count--) {
            *p++ = pair;
        }
    }
}

static void restore_map(void)
{
    // Restore the previous frame's occupied tiles, merging adjacent tiles into one blit.
    for (int row = 0; row < 13; row++) {
        uint32_t mask = map_dirty[row];
        map_dirty[row] = 0;
        int y = row * 16;
        int bottom = y + 16 < 205 ? y + 16 : 205;

        while (mask) {
            int first = __builtin_ctz(mask);
            int last = first + 1;
            while (last < 21 && (mask & (1u << last))) {
                last++;
            }
            mask &= ~(((1u << last) - 1) & ~((1u << first) - 1));

            int x = first * 16;
            int right = last * 16 < 326 ? last * 16 : 326;
            unsigned part = 0;
            while (world_clip(x, y, right - x, bottom - y, &part)) {
                dsubimage(x, y, &img_meadow, x, y, right - x, bottom - y, DIMAGE_NONE);
            }
        }
    }
}

static void rounded(int x, int y, int width, int height, int color)
{
    fill(x + 2, y, width - 4, 1, color);
    fill(x + 1, y + 1, width - 2, 1, color);
    fill(x, y + 2, width, height - 4, color);
    fill(x + 1, y + height - 2, width - 2, 1, color);
    fill(x + 2, y + height - 1, width - 4, 1, color);
}

static void box(int x, int y, int width, int height, int color, int edge, int border)
{
    rounded(x, y, width, height, edge);
    rounded(x + border, y + border, width - 2 * border, height - 2 * border, color);
}

static void label(int x, int y, int width, int color, const char *text, int align, int title)
{
    const font_t *font = title ? &font_game_title : &font_game_ui;
    int rendered;

    // Large totals use the body font to fit the available counter width.
    if (title) {
        dsize(text, font, &rendered, NULL);
        if (rendered > width) {
            font = &font_game_ui;
        }
    }

    dfont(font);
    const char *end = drsize(text, font, width, &rendered);

    if (align == DTEXT_CENTER) {
        x -= rendered >> 1;
    }

    if (align == DTEXT_RIGHT) {
        x -= rendered - 1;
    }

    if (color == C_WHITE || color == CREAM || color == GOLD) {
        dtext_opt(x, y + 1, BROWN, C_NONE, DTEXT_LEFT, DTEXT_TOP, text, end - text);
    }
    dtext_opt(x, y, color, C_NONE, DTEXT_LEFT, DTEXT_TOP, text, end - text);
}

static int wrapped(int x, int y, int width, int color, const char *text, int lines)
{
    dfont(&font_game_ui);
    int line;
    for (line = 0; line < lines && *text; line++) {
        const char *end = drsize(text, NULL, width, NULL);
        if (*end && *end != ' ') {
            const char *word = end;
            while (word > text && word[-1] != ' ') {
                word--;
            }
            if (word > text) {
                end = word - 1;
            }
        }

        if (color == C_WHITE || color == CREAM || color == GOLD) {
            dtext_opt(x, y + line * 10 + 1, BROWN, C_NONE, DTEXT_LEFT, DTEXT_TOP, text, end - text);
        }
        dtext_opt(x, y + line * 10, color, C_NONE, DTEXT_LEFT, DTEXT_TOP, text, end - text);
        text = end;
        while (*text == ' ') {
            text++;
        }
    }
    return line;
}

static int modal_row_visible(const Tower *t, int row)
{
    if (row < 3) {
        return row >= 0 && tower_defs[t->type] && tower_defs[t->type]->caps[row];
    }
    if (row == COLLECT) {
        const TowerProfile *p = tower_profile(t);
        return p && (t->type == 16 || (t->type == 19 && p->tiers[2] >= 4));
    }
    return row == SELL;
}

static int modal_next_row(const Tower *t, int row, int step)
{
    for (int next = row + step; next >= 0 && next <= SELL; next += step) {
        if (modal_row_visible(t, next)) {
            return next;
        }
    }
    return row;
}

static void hud(void)
{
    char text[32];
    dsubimage(8, 4, &img_ui_hud, 0, 0, 17, 14, DIMAGE_NONE);
    snprintf(text, sizeof text, "%u", game.lives);
    label(29, 1, 32, C_WHITE, text, DTEXT_LEFT, 1);

    dsubimage(72, 4, &img_ui_hud, 17, 0, 16, 14, DIMAGE_NONE);
    snprintf(text, sizeof text, "$%lu", (unsigned long)game.cash / 100);
    label(92, 1, 91, GOLD, text, DTEXT_LEFT, 1);

    snprintf(text, sizeof text, game.round > 60 ? "%lu" : "%lu/60", (unsigned long)game.round);
    int width;
    dsize(text, &font_game_title, &width, NULL);
    if (width > 75) {
        dsize(text, &font_game_ui, &width, NULL);
    }
    label(306 - width, 6, 34, CREAM, "ROUND", DTEXT_RIGHT, 0);
    label(312, 1, 75, C_WHITE, text, DTEXT_RIGHT, 1);
}

static void selector(void)
{
    char text[16];
    fill(326, 0, 70, 205, WOOD);

    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 2; col++) {
            int n = (ui.first_row + row) * 2 + col;
            int x = 334 + col * 32;
            int y = 1 + row * 34;
            dsubimage(x, y, &img_monkey_portraits, col * 24, (ui.first_row + row) * 24, 24, 24,
                      DIMAGE_NONE);
            unsigned price = tower_defs[n]->price;
            snprintf(text, sizeof text, "%u", price);
            label(x + 12, y + 25, 30, game.cash >= price * 100u ? CREAM : RED_UI, text,
                  DTEXT_CENTER, 0);
            if (n == ui.selected) {
                drect_border(x - 1, y - 1, x + 24, y + 24, C_NONE, 1, C_WHITE);
            }
        }
    }

    fill(394, 4, 2, 194, BROWN);
    fill(394, 4 + ui.first_row * 19, 2, 116, GOLD);
}

static void softkey(int slot, const char *text, int fill_color, int enabled, int focused)
{
    int x = slot * 66 + 2;
    box(x, 205, 62, 18, fill_color, focused ? C_WHITE : BROWN, 1);
    label(x + 31, 209, 58, enabled ? C_WHITE : CREAM, text, DTEXT_CENTER, 0);
}

static void footer(void)
{
    fill(0, 205, 396, 19, BROWN);
    if (ui.mode == SELECT) {
        softkey(0, game.won ? "FREEPLAY" : game.running ? "ROUND" : "START",
                game.running ? DISABLED : GREEN_UI,
                !game.running, 0);
        label(68, 209, 192, C_WHITE, "Arrows: select  EXE: place", DTEXT_LEFT, 0);
        softkey(4, game.speed == 1 ? "1x" : "3x", BLUE_UI, 1, 0);
        softkey(5, "UPGRADES", BLUE_UI, game.tower_count != 0, 0);
    } else if (ui.mode == MODAL) {
        label(8, 209, 380, C_WHITE, "Arrows: navigate  EXE: confirm  EXIT: back", DTEXT_LEFT, 0);
    } else if (ui.mode == PLACE) {
        int valid = can_place_monkey(ui.selected, terrain_x(ui.x), terrain_y(ui.y), -1);
        const char *status = valid ? "READY" : "BLOCKED";
        if (game.cash < tower_defs[ui.selected]->price * 100u) {
            status = "NO CASH";
            valid = 0;
        }
        label(8, 209, 60, valid ? GOLD : RED_UI, status, DTEXT_LEFT, 0);
        label(72, 209, 316, C_WHITE, "Arrows: move  EXE: place  EXIT: cancel", DTEXT_LEFT, 0);
    } else {
        label(8, 209, 380, C_WHITE, "Arrows: monkey  EXE: upgrades  EXIT: back", DTEXT_LEFT, 0);
    }

    if (game.won || game.lost || game.pool_full) {
        fill(66, 205, 196, 19, BROWN);
        label(164, 209, 190, game.lost ? RED_UI : GOLD,
              game.pool_full ? "Object capacity reached"
              : game.won     ? "VICTORY! F1: freeplay"
                             : "GAME OVER",
              DTEXT_CENTER, 0);
    }
}

static void enlarged(const bopti_image_t *image, int sx, int sy, int size, int px, int py)
{
    const int8_t *pixels = image->data;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int index = pixels[(sy + y) * image->stride + sx + x];
            if (index == -128) {
                continue;
            }
            uint16_t color = image->palette[index + 128];
            uint16_t *p = gint_vram + (py + y * 2) * DWIDTH + px + x * 2;
            p[0] = p[1] = p[DWIDTH] = p[DWIDTH + 1] = color;
        }
    }
}

static void modal_action(int x, int width, int row, const char *name, const char *value,
                         int fill_color, int edge)
{
    box(x, 175, width, 25, fill_color, ui.row == row ? C_WHITE : edge, ui.row == row ? 2 : 1);
    label(x + width / 2, 179, width - 8, C_WHITE, name, DTEXT_CENTER, 0);
    label(x + width / 2, 190, width - 8, C_WHITE, value, DTEXT_CENTER, 0);
}

static void upgrade_icon(unsigned index, int x, int y, int large)
{
    unsigned sx = index % 14 * 16;
    unsigned sy = index / 14 * 16;
    if (large)
        enlarged(&img_upgrade_icons, sx, sy, 16, x, y);
    else
        dsubimage(x, y, &img_upgrade_icons, sx, sy, 16, 16, DIMAGE_NONE);
}

static void modal(void)
{
    Tower *t = &game.towers[ui.tower];
    const TowerProfile *p = tower_profile(t);
    const TowerDef *definition = tower_defs[t->type];
    char text[80];

    box(MODAL_X, MODAL_Y, MODAL_WIDTH, MODAL_HEIGHT, TAN, BROWN, 2);
    label(18, 27, 185, C_WHITE, definition->name, DTEXT_LEFT, 1);
    snprintf(text, sizeof text, "Pops %lu", (unsigned long)t->pops);
    label(296, 27, 92, C_WHITE, text, DTEXT_RIGHT, 0);
    dline(301, 27, 308, 34, BROWN);
    dline(308, 27, 301, 34, BROWN);

    box(16, 44, 60, 100, SKY, BLUE_EDGE, 1);

    for (int y = 46; y < 142; y += 8) {
        fill(18, y, 56, 8, SKY - ((y - 46) / 8 << 5));
    }
    int portrait = appearance(t)->portrait;
    enlarged(&img_upgrade_portraits, portrait % 2 * 24, portrait / 2 * 24, 24, 22, 69);

    int visible = 0;

    for (int path = 0; path < 3; path++) {
        if (!modal_row_visible(t, path)) {
            continue;
        }
        int y = 44 + visible++ * 43;
        int tier = p ? p->tiers[path] : 0;
        const Upgrade *next = tower_next_upgrade(t, path);
        const Upgrade *owned = tier ? &definition->upgrades[path][tier - 1] : NULL;
        int allowed = game_upgrade_allowed(ui.tower, path);
        int affordable = next && game.cash >= next->price * 100u;
        int purchasable = next && allowed && affordable;

        box(82, y, 86, 41, WOOD, BROWN, 1);
        box(170, y, 136, 41, purchasable ? GREEN_UI : DISABLED,
            ui.row == path ? C_WHITE
            : purchasable  ? GREEN_EDGE
                           : DISABLED_EDGE,
            ui.row == path ? 2 : 1);
        int progress = tier * 66 / definition->caps[path];
        for (int mark = 0; mark < 3; mark++) {
            int x = 86 + mark * 27;
            int filled = progress - mark * 22;
            if (filled > 22) {
                filled = 22;
            }
            drect_border(x, y + 3, x + 23, y + 6, TAN, 1, BROWN);
            if (filled > 0) {
                fill(x + 1, y + 4, filled, 2, LIME);
            }
        }

        if (owned) {
            int lines = wrapped(105, y + 8, 59, CREAM, owned->name, 3);
            upgrade_icon(owned->icon, 86, y + 21, 0);
            if (lines < 3) {
                label(105, y + 29, 59, LIME, "OWNED", DTEXT_LEFT, 0);
            }
        } else {
            wrapped(90, y + 14, 70, CREAM, "Not upgraded", 2);
        }

        if (next) {
            upgrade_icon(next->icon, 175, y + 5, 1);
            wrapped(211, y + 4, 91, C_WHITE, next->name, 2);
            if (allowed) {
                snprintf(text, sizeof text, "$%u", next->price);
            } else {
                snprintf(text, sizeof text, "LOCKED");
            }
            label(211, y + 28, 91, allowed && !affordable ? RED_UI : C_WHITE, text, DTEXT_LEFT, 0);
        } else {
            label(238, y + 15, 128, C_WHITE, "MAX UPGRADES", DTEXT_CENTER, 0);
        }
    }

    if (modal_row_visible(t, COLLECT)) {
        modal_action(16, 105, COLLECT, "Collect", t->type == 16 ? "Bananas" : "Traps", BLUE_UI,
                     BLUE_EDGE);
    }

    snprintf(text, sizeof text, "$%lu", (unsigned long)(t->spent * 7 / 10 / 100));
    modal_action(185, 121, SELL, "SELL", text, ORANGE, ORANGE_EDGE);
}

static void modal_update(int refresh)
{
    const Tower *t = &game.towers[ui.tower];
    unsigned affordable = 0;

    for (int path = 0; path < 3; path++) {
        const Upgrade *next = tower_next_upgrade(t, path);
        if (next && game.cash >= next->price * 100u) {
            affordable |= 1u << path;
        }
    }

    if (refresh || !modal_cache.valid || modal_cache.tower != ui.tower ||
        modal_cache.type != t->type || modal_cache.profile != t->profile ||
        modal_cache.row != ui.row || modal_cache.spent != t->spent ||
        modal_cache.affordable != affordable) {
        modal();
    } else if (modal_cache.pops != t->pops) {
        char text[32];
        struct dwindow window = dwindow_set((struct dwindow){205, 27, 297, 39});
        fill(205, 27, 92, 12, TAN);
        snprintf(text, sizeof text, "Pops %lu", (unsigned long)t->pops);
        label(296, 27, 92, C_WHITE, text, DTEXT_RIGHT, 0);
        dwindow_set(window);
    }

    modal_cache.valid = 1;
    modal_cache.tower = ui.tower;
    modal_cache.type = t->type;
    modal_cache.profile = t->profile;
    modal_cache.row = ui.row;
    modal_cache.pops = t->pops;
    modal_cache.spent = t->spent;
    modal_cache.affordable = affordable;
}

static uint32_t selector_state(void)
{
    uint32_t key = ui.selected | ((unsigned)ui.first_row << 5);

    for (int n = 0; n < 12; n++) {
        unsigned price = tower_defs[ui.first_row * 2 + n]->price;
        if (game.cash >= price * 100u) {
            key |= 1u << (8 + n);
        }
    }

    return key;
}

static uint32_t footer_state(void)
{
    uint32_t key = ui.mode | ((unsigned)game.speed << 5) |
                   ((unsigned)game.running << 7) | ((unsigned)game.won << 8) |
                   ((unsigned)game.lost << 9) | ((unsigned)game.pool_full << 10) |
                   ((game.tower_count != 0) << 11);

    if (ui.mode == PLACE) {
        key |= can_place_monkey(ui.selected, terrain_x(ui.x), terrain_y(ui.y), -1) << 12;
        key |= (game.cash >= tower_defs[ui.selected]->price * 100u) << 13;
    }

    return key;
}

void ui_draw(void)
{
    if (render_vram != gint_vram) {
        render_vram = gint_vram;
        selector_key = footer_key = UINT32_MAX;
        modal_cache.valid = 0;
        dclear(C_BLACK);
        map_mark(0, 0, 326, 205);
    }

    if (previous_mode == MODAL && ui.mode != MODAL) {
        map_mark(MODAL_X, MODAL_Y, MODAL_WIDTH, MODAL_HEIGHT);
        modal_cache.valid = 0;
    }

    unsigned cash = game.cash / 100;
    if (cash != hud_cash || game.lives != hud_lives || game.round != hud_round) {
        map_mark(8, 0, 305, 20);
    }
    int redraw_hud = (map_dirty[0] | map_dirty[1]) & 0xfffffu;

    struct dwindow window = dwindow_set((struct dwindow){0, 0, 326, 205});
    restore_map();

    unsigned seen = 0;

    for (int i = 0; i < CASH_DROP_LIMIT && seen < game.cash_drop_count; i++) {
        CashDrop *drop = &game.cash_drops[i];
        if (!drop->active) {
            continue;
        }
        seen++;
        int x = map_x(drop->x);
        int y = map_y(drop->y);
        world_sprite(&img_support_sprites, ART_BANANA, 16, support_bounds[ART_BANANA], x, y);
    }

    seen = 0;

    for (int i = 0; i < SENTRY_LIMIT && seen < game.sentry_count; i++) {
        Sentry *s = &game.sentries[i];
        if (!s->active) {
            continue;
        }
        seen++;
        if (s->flight) {
            continue;
        }
        int x = map_x(s->x);
        int y = map_y(s->y);
        unsigned sprite = ART_SENTRY + sentry_facing[i];
        world_sprite(&img_support_sprites, sprite, 16, support_bounds[sprite], x, y);
    }

    unsigned status_pixels = 8192;

    for (unsigned i = bloon_next(0); i < BLOON_LIMIT; i = bloon_next(i + 1)) {
        Bloon *b = &game.bloons[i];
        int x = map_x(b->x);
        int y = map_y(b->y);
        if (x < 0 || x > 325 || y < 0 || y > DHEIGHT) {
            continue;
        }
        draw_bloon(i, x, y, &status_pixels);
    }

    unsigned projectile_pixels = 8192;

    for (unsigned n = 0; n < game.shot_count; n++) {
        unsigned id = game.shot_active[n];
        Shot *s = &game.shots[id];
        int x = map_x(s->x);
        int y = map_y(s->y);
        if (x < 0 || x >= 326 || y < 0 || y >= 205) {
            continue;
        }
        if (s->attack->flags & A_TRAP) {
            unsigned sprite = s->trap_full ? ART_TRAP_FULL : ART_TRAP_EMPTY;
            world_sprite(&img_support_sprites, sprite, 16, support_bounds[sprite], x, y);
        } else {
            const CombatSprite *sprite = &projectile_art[shot_styles[id]];
            if (!sprite->count)
                continue;
            if (!projectile_pixels) {
                unsigned part = 0;
                while (world_clip(x, y, 1, 1, &part)) {
                    dpixel(x, y, (s->attack->flags & A_SPIKE) ? C_WHITE : 0xffc0);
                }
                map_mark(x, y, 1, 1);
                continue;
            }
            unsigned frame = 0;
            if (sprite->motion == 1)
                frame = animation_direction(s->vx, s->vy);
            else if (sprite->motion == 2)
                frame = s->age / 360 & 3;
            else if (sprite->motion == 3) {
                frame = s->pierce ? s->pierce - 1 : 0;
                if (frame >= sprite->count)
                    frame = sprite->count - 1;
            }
            const SpriteBounds *bounds = &combat_bounds[sprite->first + frame];
            unsigned pixels = bounds->width * bounds->height;
            // Crowded scenes retain every projectile marker without unlimited sprite overdraw.
            projectile_pixels = pixels < projectile_pixels ? projectile_pixels - pixels : 0;
            combat_sprite(sprite->first + frame, x, y);
        }
    }

    seen = 0;

    for (int i = 0; i < TOWER_LIMIT && seen < game.tower_count; i++) {
        Tower *t = &game.towers[i];
        if (!t->active) {
            continue;
        }
        seen++;
        tower_sprite(i, map_x(t->x), map_y(t->y), 0);
        const TowerProfile *p = tower_profile(t);
        if (p && (p->support & S_AIR)) {
            tower_sprite(i, map_x(t->air_x), map_y(t->air_y), 1);
        }
    }

    attack_effects();

    if (ui.mode == PLACE) {
        int valid = can_place_monkey(ui.selected, terrain_x(ui.x), terrain_y(ui.y), -1);
        monkey(ui.selected, ui.x, ui.y,
               valid ? &img_monkey_sprites : &red_sprites);
        dline(ui.x - 5, ui.y, ui.x + 5, ui.y, C_WHITE);
        dline(ui.x, ui.y - 5, ui.x, ui.y + 5, C_WHITE);
    }

    if (ui.mode == PICK) {
        Tower *t = &game.towers[ui.tower];
        int x = map_x(t->x);
        int y = map_y(t->y);
        drect_border(x - 9, y - 9, x + 8, y + 8, C_NONE, 1, C_WHITE);
        map_mark(x - 9, y - 9, 18, 18);
    }

    dwindow_set(window);
    uint32_t next_selector = selector_state();
    int redraw_selector = next_selector != selector_key ||
                          (previous_mode == MODAL && ui.mode != MODAL);
    if (redraw_selector) {
        selector();
        selector_key = next_selector;
    }

    if (redraw_hud || ((map_dirty[0] | map_dirty[1]) & 0xfffffu)) {
        hud();
    }
    hud_cash = cash;
    hud_lives = game.lives;
    hud_round = game.round;

    if (ui.mode == MODAL) {
        modal_update(redraw_selector);
    }

    uint32_t next_footer = footer_state();
    if (next_footer != footer_key) {
        footer();
        footer_key = next_footer;
    }

    previous_mode = ui.mode;
    if (exit_confirmation) {
        box(60, 61, 276, 102, TAN, BROWN, 2);
        label(198, 73, 254, C_WHITE, "Exit to menu?", DTEXT_CENTER, 1);
        label(198, 96, 254, BROWN, "Are you absolutely sure you wanna go?", DTEXT_CENTER, 0);
        box(111, 114, 174, 25, ORANGE, ORANGE_EDGE, 1);
        label(198, 120, 160, C_WHITE, "F1: EXIT BLOONS CG", DTEXT_CENTER, 0);
        label(198, 145, 254, BROWN, "Click any other key to go back to the game", DTEXT_CENTER, 0);
    }
    dupdate();
}

static void modal_activate(void)
{
    Tower *t = &game.towers[ui.tower];
    if (ui.row < 3) {
        game_upgrade(ui.tower, ui.row);
    } else if (ui.row == COLLECT) {
        const TowerProfile *p = tower_profile(t);
        if (t->type == 16 && p) {
            support_collect(t->x, t->y, p->income_radius);
        } else if (t->type == 19 && p && p->tiers[2] >= 4) {
            trap_collect(t->x, t->y);
        }
    } else if (ui.row == SELL) {
        game_sell(ui.tower);
        ui.mode = SELECT;
    }
}

int ui_paused(void)
{
    return exit_confirmation;
}

int ui_key(int key)
{
    if (exit_confirmation) {
        exit_confirmation = 0;
        render_vram = NULL;
        return key != KEY_F1;
    }

    if (key == KEY_MENU) {
        exit_confirmation = 1;
        return 1;
    }

    if (key == KEY_F1 && game.won) {
        game_start_round();
        return 1;
    }

    if (ui.mode == MODAL) {
        Tower *t = &game.towers[ui.tower];
        if (key == KEY_EXIT) {
            ui.mode = PICK;
            return 1;
        }

        if (key == KEY_UP) {
            ui.row = modal_next_row(t, ui.row, -1);
        }
        if (key == KEY_DOWN) {
            ui.row = modal_next_row(t, ui.row, 1);
        }

        if (key == KEY_LEFT && ui.row >= COLLECT && modal_row_visible(t, COLLECT)) {
            ui.row = COLLECT;
        }
        if (key == KEY_RIGHT && ui.row >= COLLECT) {
            ui.row = SELL;
        }

        // Focus changes never spend money or sell a tower.
        if (key == KEY_EXE) {
            modal_activate();
        }

        return 1;
    }

    if (ui.mode == PICK) {
        if (key == KEY_EXIT) {
            ui.mode = SELECT;
            return 1;
        }

        if (key == KEY_LEFT || key == KEY_UP) {
            ui.tower = next_tower(ui.tower, -1);
        }
        if (key == KEY_RIGHT || key == KEY_DOWN) {
            ui.tower = next_tower(ui.tower, 1);
        }

        if (key == KEY_EXE || key == KEY_F6) {
            ui.mode = MODAL;
            ui.row = modal_next_row(&game.towers[ui.tower], -1, 1);
        }

        return 1;
    }

    if (ui.mode == PLACE) {
        if (key == KEY_EXIT) {
            ui.mode = SELECT;
            return 1;
        }

        if (key == KEY_EXE && game_place(ui.selected, terrain_x(ui.x), terrain_y(ui.y)) >= 0) {
            ui.mode = SELECT;
        }

        if (key == KEY_LEFT) {
            ui.x -= 2;
        }
        if (key == KEY_RIGHT) {
            ui.x += 2;
        }
        if (key == KEY_UP) {
            ui.y -= 2;
        }
        if (key == KEY_DOWN) {
            ui.y += 2;
        }

        if (ui.x < 8) {
            ui.x = 8;
        }
        if (ui.x > 318) {
            ui.x = 318;
        }
        if (ui.y < 8) {
            ui.y = 8;
        }
        if (ui.y > 197) {
            ui.y = 197;
        }

        return 1;
    }

    if (key == KEY_EXIT) {
        return 1;
    }

    if (key == KEY_F1) {
        game_start_round();
    }

    if (key == KEY_F5) {
        game.speed = game.speed == 1 ? 3 : 1;
    }

    if (key == KEY_F6 && game.tower_count) {
        ui.tower = next_tower(-1, 1);
        ui.mode = PICK;
    }

    if (key == KEY_EXE) {
        ui.mode = PLACE;
        ui.x = 163;
        ui.y = 102;
    }

    if (key == KEY_UP && ui.selected >= 2) {
        ui.selected -= 2;
    }

    if (key == KEY_DOWN && ui.selected + 2 < MONKEY_COUNT) {
        ui.selected += 2;
    }

    if (key == KEY_LEFT && ui.selected % 2) {
        ui.selected--;
    }

    if (key == KEY_RIGHT && !(ui.selected % 2)) {
        ui.selected++;
    }

    if (ui.selected / 2 < ui.first_row) {
        ui.first_row = ui.selected / 2;
    }

    if (ui.selected / 2 >= ui.first_row + 6) {
        ui.first_row = ui.selected / 2 - 5;
    }

    return 1;
}
