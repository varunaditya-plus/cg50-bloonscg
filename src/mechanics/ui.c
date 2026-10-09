#include "game.h"
#include <gint/display.h>
#include <gint/keyboard.h>
#include <stdio.h>

// Portraits/icons: https://www.spriters-resource.com/pc_computer/bloonstd6/
// Monkey models: https://models.spriters-resource.com/pc_computer/bloonstd6/
extern const bopti_image_t img_meadow, img_monkey_portraits, img_monkey_sprites;
extern const bopti_image_t img_bloons, img_upgrade_icons, img_ui_hud;
extern const font_t font_game_ui, font_game_title;

enum { SELECT, PLACE, PICK, MODAL };
enum { COLLECT = 3, SELL };

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
    if (ui.mode == MODAL && x >= 14 && y >= 22 && bottom < 202) {
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
    if (x >= 14 && y >= 22 && bottom <= 202) {
        return 0;
    }

    // The opaque modal leaves these map bands and its rounded corner pixels exposed.
    static const struct dwindow exposed[] = {
        {0, 0, 326, 22}, {0, 202, 326, 205}, {0, 22, 12, 202},
        {12, 22, 14, 23}, {12, 23, 13, 24}, {12, 200, 13, 201}, {12, 201, 14, 202}
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

static void monkey(int type, int x, int y, const bopti_image_t *img)
{
    unsigned part = 0;
    while (world_clip(6 + x - 8, 4 + y - 8, 16, 16, &part)) {
        dsubimage(6 + x - 8, 4 + y - 8, img, type % 2 * 16, type / 2 * 16, 16, 16, DIMAGE_NONE);
    }
    map_mark(6 + x - 8, 4 + y - 8, 16, 16);
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
            if (y < 4) {
                fill(x, y, right - x, 4 - y, C_BLACK);
            }
            if (x < 6) {
                fill(x, y, 6 - x, bottom - y, C_BLACK);
            }

            int left = x < 6 ? 6 : x;
            int top = y < 4 ? 4 : y;
            unsigned part = 0;
            while (world_clip(left, top, right - left, bottom - top, &part)) {
                dsubimage(left, top, &img_meadow, left - 6, top - 4, right - left, bottom - top,
                          DIMAGE_NONE);
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

static void wrapped(int x, int y, int width, int color, const char *text)
{
    dfont(&font_game_ui);
    for (int line = 0; line < 2 && *text; line++) {
        const char *end = drsize(text, NULL, width, NULL);
        if (*end) {
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

static const char *modal_confirm(const Tower *t)
{
    if (ui.row < 3) {
        const Upgrade *next = tower_next_upgrade(t, ui.row);
        if (!next) {
            return "MAX";
        }
        if (!game_upgrade_allowed(ui.tower, ui.row)) {
            return "LOCKED";
        }
        return game.cash >= next->price * 100u ? "BUY" : "NEED $";
    }
    return ui.row == SELL ? "SELL" : "COLLECT";
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

    label(199, 6, 34, CREAM, "ROUND", DTEXT_LEFT, 0);
    snprintf(text, sizeof text, "%u/60", game.round);
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
        softkey(0, game.running ? "ROUND" : "START", game.running ? DISABLED : GREEN_UI,
                !game.running, 0);
        label(68, 209, 192, C_WHITE, "Arrows: select  EXE: place", DTEXT_LEFT, 0);
        softkey(4, game.speed == 1 ? "1x" : "3x", BLUE_UI, 1, 0);
        softkey(5, "UPGRADES", BLUE_UI, game.tower_count != 0, 0);
    } else if (ui.mode == MODAL) {
        const Tower *t = &game.towers[ui.tower];
        char text[12];

        for (int path = 0; path < 3; path++) {
            if (modal_row_visible(t, path)) {
                snprintf(text, sizeof text, "PATH %d", path + 1);
                softkey(path, text, BLUE_UI, 1, ui.row == path);
            }
        }

        if (modal_row_visible(t, COLLECT)) {
            softkey(4, "COLLECT", BLUE_UI, 1, ui.row == COLLECT);
        }

        const char *confirm = modal_confirm(t);
        const Upgrade *next = ui.row < 3 ? tower_next_upgrade(t, ui.row) : NULL;
        int ready = ui.row >= 3 || (next && game_upgrade_allowed(ui.tower, ui.row) &&
                                    game.cash >= next->price * 100u);
        softkey(5, confirm, ui.row == SELL ? ORANGE : ready ? GREEN_UI : DISABLED, ready, 0);
    } else if (ui.mode == PLACE) {
        int valid = can_place_monkey(ui.selected, ui.x, ui.y, -1);
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
              : game.won     ? "VICTORY! Round 60 complete"
                             : "GAME OVER",
              DTEXT_CENTER, 0);
    }
}

static void portrait(int type, int px, int py)
{
    const int8_t *pixels = img_monkey_portraits.data;
    for (int y = 0; y < 24; y++) {
        for (int x = 0; x < 24; x++) {
            int index =
                pixels[(type / 2 * 24 + y) * img_monkey_portraits.stride + type % 2 * 24 + x];
            if (index == -128) {
                continue;
            }
            uint16_t color = img_monkey_portraits.palette[index + 128];
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

static void modal(void)
{
    Tower *t = &game.towers[ui.tower];
    const TowerProfile *p = tower_profile(t);
    const TowerDef *definition = tower_defs[t->type];
    char text[80];

    box(12, 22, 372, 180, TAN, BROWN, 2);
    label(22, 27, 270, C_WHITE, definition->name, DTEXT_LEFT, 1);
    snprintf(text, sizeof text, "Pops %lu", (unsigned long)t->pops);
    label(357, 27, 92, C_WHITE, text, DTEXT_RIGHT, 0);
    dline(369, 27, 376, 34, BROWN);
    dline(376, 27, 369, 34, BROWN);

    box(22, 44, 85, 100, SKY, BLUE_EDGE, 1);

    for (int y = 46; y < 142; y += 8) {
        fill(24, y, 81, 8, SKY - ((y - 46) / 8 << 5));
    }
    portrait(t->type, 40, 69);

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

        box(116, y, 113, 41, WOOD, BROWN, 1);
        box(230, y, 145, 41, purchasable ? GREEN_UI : DISABLED,
            ui.row == path ? C_WHITE
            : purchasable  ? GREEN_EDGE
                           : DISABLED_EDGE,
            ui.row == path ? 2 : 1);
        for (int level = 0; level < definition->caps[path]; level++) {
            drect_border(120, y + 3 + level * 8, 125, y + 8 + level * 8, level < tier ? LIME : TAN,
                         1, BROWN);
        }

        wrapped(131, y + 3, 93, CREAM, owned ? owned->name : "Not upgraded");
        if (owned) {
            dsubimage(132, y + 23, &img_upgrade_icons, owned->icon % 14 * 16, owned->icon / 14 * 16,
                      16, 16, DIMAGE_NONE);
            label(154, y + 28, 69, LIME, "OWNED", DTEXT_LEFT, 0);
        }

        if (next) {
            wrapped(237, y + 3, 131, C_WHITE, next->name);
            dsubimage(238, y + 23, &img_upgrade_icons, next->icon % 14 * 16, next->icon / 14 * 16,
                      16, 16, DIMAGE_NONE);
            if (allowed) {
                snprintf(text, sizeof text, "$%u", next->price);
            } else {
                snprintf(text, sizeof text, "LOCKED");
            }
            label(261, y + 22, 107, allowed && !affordable ? RED_UI : C_WHITE, text, DTEXT_LEFT,
                  allowed);
        } else {
            label(302, y + 15, 135, C_WHITE, "MAX UPGRADES", DTEXT_CENTER, 0);
        }
    }

    if (modal_row_visible(t, COLLECT)) {
        modal_action(22, 105, COLLECT, "Collect", t->type == 16 ? "Bananas" : "Traps", BLUE_UI,
                     BLUE_EDGE);
    }

    snprintf(text, sizeof text, "$%lu", (unsigned long)(t->spent * 7 / 10 / 100));
    modal_action(254, 121, SELL, "SELL", text, ORANGE, ORANGE_EDGE);
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
        struct dwindow window = dwindow_set((struct dwindow){266, 27, 358, 39});
        fill(266, 27, 92, 12, TAN);
        label(22, 27, 270, C_WHITE, tower_defs[t->type]->name, DTEXT_LEFT, 1);
        snprintf(text, sizeof text, "Pops %lu", (unsigned long)t->pops);
        label(357, 27, 92, C_WHITE, text, DTEXT_RIGHT, 0);
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
    uint32_t key = ui.mode | ((unsigned)ui.row << 2) | ((unsigned)game.speed << 5) |
                   ((unsigned)game.running << 7) | ((unsigned)game.won << 8) |
                   ((unsigned)game.lost << 9) | ((unsigned)game.pool_full << 10) |
                   ((game.tower_count != 0) << 11);

    if (ui.mode == PLACE) {
        key |= can_place_monkey(ui.selected, ui.x, ui.y, -1) << 12;
        key |= (game.cash >= tower_defs[ui.selected]->price * 100u) << 13;
    }

    if (ui.mode == MODAL) {
        const Tower *t = &game.towers[ui.tower];
        key |= modal_row_visible(t, COLLECT) << 17;

        if (ui.row < 3) {
            const Upgrade *next = tower_next_upgrade(t, ui.row);
            key |= (next != NULL) << 14;
            key |= game_upgrade_allowed(ui.tower, ui.row) << 15;
            key |= (next && game.cash >= next->price * 100u) << 16;
        }
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
        map_mark(12, 22, 372, 180);
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
        int x = 6 + screen_x(drop->x);
        int y = 4 + screen_y(drop->y);
        unsigned part = 0;
        while (world_clip(x - 1, y - 1, 3, 3, &part)) {
            drect(x - 1, y - 1, x + 1, y + 1, 0xffe0);
        }
        map_mark(x - 1, y - 1, 3, 3);
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
        int x = 6 + screen_x(s->x);
        int y = 4 + screen_y(s->y);
        unsigned part = 0;
        while (world_clip(x - 3, y - 3, 7, 7, &part)) {
            drect_border(x - 3, y - 3, x + 3, y + 3, 0x4b44, 1, C_WHITE);
        }
        map_mark(x - 3, y - 3, 7, 7);
    }

    for (unsigned i = bloon_next(0); i < BLOON_LIMIT; i = bloon_next(i + 1)) {
        Bloon *b = &game.bloons[i];
        int x = 6 + screen_x(b->x);
        int y = 4 + screen_y(b->y);
        if (x < 0 || x > 325 || y < 0 || y > DHEIGHT) {
            continue;
        }
        map_mark(x - 12, y - 12, 24, 24);
        unsigned part = 0;
        while (world_clip(x - 12, y - 12, 24, 24, &part)) {
            dsubimage(x - 12, y - 12, &img_bloons, b->type * 24, 0, 24, 24, DIMAGE_NONE);
            if (b->flags & FORTIFIED) {
                drect_border(x - 4, y - 5, x + 4, y + 5, C_NONE, 1, 0x8c41);
            }
            if (b->flags & CAMO) {
                dpixel(x - 2, y, 0x2445);
                dpixel(x + 2, y - 2, 0x2445);
            }
            if (b->glue) {
                dpixel(x, y, 0xff80);
            }
            if (b->freeze) {
                drect_border(x - 3, y - 4, x + 3, y + 4, C_NONE, 1, 0x7fff);
            }
        }
    }

    for (unsigned n = 0; n < game.shot_count; n++) {
        Shot *s = &game.shots[game.shot_active[n]];
        int x = 6 + screen_x(s->x);
        int y = 4 + screen_y(s->y);
        if (x < 6 || x >= 326 || y < 4 || y >= 220) {
            continue;
        }
        if (s->attack->flags & A_TRAP) {
            unsigned part = 0;
            while (world_clip(x - 4, y - 3, 9, 7, &part)) {
                drect_border(x - 4, y - 3, x + 4, y + 3, s->trap_full ? 0xffe0 : 0x83a5,
                             1, C_WHITE);
            }
            map_mark(x - 4, y - 3, 9, 7);
        } else {
            unsigned part = 0;
            while (world_clip(x, y, 1, 1, &part)) {
                dpixel(x, y, (s->attack->flags & A_SPIKE) ? C_WHITE : 0xffc0);
            }
            map_mark(x, y, 1, 1);
        }
    }

    seen = 0;

    for (int i = 0; i < TOWER_LIMIT && seen < game.tower_count; i++) {
        Tower *t = &game.towers[i];
        if (!t->active) {
            continue;
        }
        seen++;
        monkey(t->type, screen_x(t->x), screen_y(t->y), &img_monkey_sprites);
        const TowerProfile *p = tower_profile(t);
        if (p && (p->support & S_AIR)) {
            monkey(t->type, screen_x(t->air_x), screen_y(t->air_y), &img_monkey_sprites);
        }
    }

    if (ui.mode == PLACE) {
        monkey(ui.selected, ui.x, ui.y,
               can_place_monkey(ui.selected, ui.x, ui.y, -1) ? &img_monkey_sprites : &red_sprites);
        dline(6 + ui.x - 5, 4 + ui.y, 6 + ui.x + 5, 4 + ui.y, C_WHITE);
        dline(6 + ui.x, 4 + ui.y - 5, 6 + ui.x, 4 + ui.y + 5, C_WHITE);
    }

    if (ui.mode == PICK) {
        Tower *t = &game.towers[ui.tower];
        int x = 6 + screen_x(t->x);
        int y = 4 + screen_y(t->y);
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

int ui_key(int key)
{
    if (key == KEY_MENU) {
        return 0;
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

        int function = keycode_function(key);
        if (function >= 1 && function <= 3 && modal_row_visible(t, function - 1)) {
            ui.row = function - 1;
        }
        if (key == KEY_F5 && modal_row_visible(t, COLLECT)) {
            ui.row = COLLECT;
        }

        // Focus changes never spend money or sell a tower.
        if (key == KEY_EXE || key == KEY_F6) {
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

        if (key == KEY_EXE && game_place(ui.selected, ui.x, ui.y) >= 0) {
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
        if (ui.x > 312) {
            ui.x = 312;
        }
        if (ui.y < 8) {
            ui.y = 8;
        }
        if (ui.y > 193) {
            ui.y = 193;
        }

        return 1;
    }

    if (key == KEY_EXIT) {
        return 0;
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
        ui.x = 162;
        ui.y = 108;
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
