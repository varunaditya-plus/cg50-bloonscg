#include "game.h"
#include <gint/display.h>
#include <gint/keyboard.h>
#include <stdio.h>

// Portraits/icons: https://www.spriters-resource.com/pc_computer/bloonstd6/
// Monkey models: https://models.spriters-resource.com/pc_computer/bloonstd6/

extern const bopti_image_t img_meadow, img_monkey_portraits, img_monkey_sprites;
extern const bopti_image_t img_bloons, img_upgrade_icons;

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

void ui_init(void)
{
    ui = (UI){0};
    red_sprites = img_monkey_sprites;
    red_sprites.palette = red_palette;

    for (int i = 0; i < img_monkey_sprites.color_count; i++) {
        uint16_t c = img_monkey_sprites.palette[i];
        red_palette[i] =
            ((((c >> 11) + 93) / 4) << 11) | ((((c >> 5) & 63) / 4) << 5) | ((c & 31) / 4);
    }
}

static void monkey(int type, int x, int y, const bopti_image_t *img)
{
    dsubimage(6 + x - 8, 4 + y - 8, img, type % 2 * 16, type / 2 * 16, 16, 16, DIMAGE_NONE);
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

static void modal(void)
{
    Tower *t = &game.towers[ui.tower];
    const TowerProfile *p = tower_profile(t);
    const TowerDef *definition = tower_defs[t->type];
    char text[80];

    drect_border(21, 20, 314, 204, 0x2104, 1, C_WHITE);
    dtext(29, 26, C_WHITE, definition ? definition->name : monkey_names[t->type]);
    if (p) {
        snprintf(text, sizeof text, "%d-%d-%d  $%lu", p->tiers[0], p->tiers[1], p->tiers[2],
                 (unsigned long)game.cash / 100);
    } else {
        snprintf(text, sizeof text, "$%lu", (unsigned long)game.cash / 100);
    }
    dtext(29, 40, C_WHITE, text);

    int visible = 0;
    for (int row = 0; row <= SELL; row++) {
        if (!modal_row_visible(t, row)) {
            continue;
        }
        int y = 58 + visible++ * 19;
        int color = C_WHITE;
        if (row == ui.row) {
            drect(25, y - 2, 309, y + 15, 0x52aa);
        }

        if (row < 3) {
            const Upgrade *next = tower_next_upgrade(t, row);
            if (next) {
                dsubimage(29, y, &img_upgrade_icons, next->icon % 14 * 16, next->icon / 14 * 16, 16,
                          16, DIMAGE_NONE);
                snprintf(text, sizeof text, "%s $%u", next->name, next->price);
                if (!game_upgrade_allowed(ui.tower, row) || game.cash < next->price * 100u) {
                    color = 0x9cf3;
                }
            } else {
                snprintf(text, sizeof text, "%s: Complete",
                         row == 0   ? "Top"
                         : row == 1 ? "Middle"
                                    : "Bottom");
            }
            dtext(49, y + 3, color, text);
        } else if (row == COLLECT) {
            dtext(30, y + 3, C_WHITE, t->type == 16 ? "Collect bananas" : "Collect filled traps");
        } else {
            snprintf(text, sizeof text, "Sell: $%lu", (unsigned long)(t->spent * 7 / 10 / 100));
            dtext(30, y + 3, C_WHITE, text);
        }
    }
}

void ui_draw(void)
{
    dclear(C_BLACK);
    dimage(6, 4, &img_meadow);

    unsigned seen = 0;
    for (int i = 0; i < CASH_DROP_LIMIT && seen < game.cash_drop_count; i++) {
        CashDrop *drop = &game.cash_drops[i];
        if (!drop->active) {
            continue;
        }
        seen++;
        int x = 6 + screen_x(drop->x);
        int y = 4 + screen_y(drop->y);
        drect(x - 1, y - 1, x + 1, y + 1, 0xffe0);
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
        drect_border(x - 3, y - 3, x + 3, y + 3, 0x4b44, 1, C_WHITE);
    }

    seen = 0;
    for (int i = 0; i < BLOON_LIMIT && seen < game.bloon_count; i++) {
        Bloon *b = &game.bloons[i];
        if (!b->active) {
            continue;
        }
        seen++;
        int x = 6 + screen_x(b->x);
        int y = 4 + screen_y(b->y);
        if (x < 0 || x > 325 || y < 0 || y > DHEIGHT) {
            continue;
        }
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

    for (unsigned n = 0; n < game.shot_count; n++) {
        Shot *s = &game.shots[game.shot_active[n]];
        int x = 6 + screen_x(s->x);
        int y = 4 + screen_y(s->y);
        if (x < 6 || x >= 326 || y < 4 || y >= 220) {
            continue;
        }
        if (s->attack->flags & A_TRAP) {
            drect_border(x - 4, y - 3, x + 4, y + 3, s->trap_full ? 0xffe0 : 0x83a5, 1, C_WHITE);
        } else {
            dpixel(x, y, (s->attack->flags & A_SPIKE) ? C_WHITE : 0xffc0);
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
    }

    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 2; col++) {
            dsubimage(330 + col * 29, 30 + row * 31, &img_monkey_portraits, col * 24,
                      (ui.first_row + row) * 24, 24, 24, DIMAGE_NONE);
        }
    }
    int focus_x = 330 + ui.selected % 2 * 29;
    int focus_y = 30 + (ui.selected / 2 - ui.first_row) * 31;
    drect_border(focus_x - 1, focus_y - 1, focus_x + 24, focus_y + 24, C_NONE, 1, C_WHITE);

    char text[80];
    drect(6, 4, 319, 17, 0x2104);
    snprintf(text, sizeof text, "Lives %u  $%lu  Round %u/60", game.lives,
             (unsigned long)game.cash / 100, game.round);
    dtext(9, 6, C_WHITE, text);
    drect(6, 206, 319, 219, 0x2104);
    snprintf(text, sizeof text, "F1:Start  F5:%ux  F6:Upgrade", game.speed);
    if (ui.mode == PLACE) {
        const TowerDef *definition = tower_defs[ui.selected];
        snprintf(text, sizeof text, "%s $%u  EXE:Place",
                 definition ? definition->name : monkey_names[ui.selected],
                 definition ? definition->price : monkey_prices[ui.selected]);
    } else if (ui.mode == PICK) {
        snprintf(text, sizeof text, "Arrows:Monkey EXE:Open EXIT:Back");
    } else if (ui.mode == MODAL) {
        snprintf(text, sizeof text, "Arrows:Option EXE:Confirm EXIT:Back");
    }
    if (game.won) {
        snprintf(text, sizeof text, "Round 60 complete!");
    }
    if (game.lost) {
        snprintf(text, sizeof text, "Game over");
    }
    if (game.pool_full) {
        snprintf(text, sizeof text, "Object capacity reached");
    }
    dtext(9, 209, C_WHITE, text);
    if (ui.mode == MODAL) {
        modal();
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
