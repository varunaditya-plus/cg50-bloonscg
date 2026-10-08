#include <gint/display.h>
#include <gint/keyboard.h>

extern const bopti_image_t img_meadow;
// Portraits: https://www.spriters-resource.com/pc_computer/bloonstd6/
extern const bopti_image_t img_monkey_portraits;
// Monkey models: https://models.spriters-resource.com/pc_computer/bloonstd6/
extern const bopti_image_t img_monkey_sprites;
// Grass mask from meadow-guide.png; 48 bytes per row, most significant bit first.
extern const uint8_t meadow_placement[];

enum {
    MONKEY_COUNT = 20,
    MONKEY_ICON_SIZE = 24,
    MONKEY_SPRITE_SIZE = 16,
    MONKEY_LIMIT = 256,
    MAP_WIDTH = 384,
    MAP_HEIGHT = 216,
    SELECTOR_X = 324,
    SELECTOR_Y = 26,
    SELECTOR_COLUMN_STEP = 29,
    SELECTOR_ROW_STEP = 31,
    SELECTOR_VISIBLE_ROWS = 6,
};

struct placed_monkey {
    int16_t x, y;
    uint8_t type;
};

static void draw_selector(int meadow_x, int meadow_y, int first_row)
{

    for (int row = 0; row < SELECTOR_VISIBLE_ROWS; row++) {
        for (int column = 0; column < 2; column++) {

            int x = meadow_x + SELECTOR_X + column * SELECTOR_COLUMN_STEP;
            int y = meadow_y + SELECTOR_Y + row * SELECTOR_ROW_STEP;
            dsubimage(x, y, &img_monkey_portraits, column * MONKEY_ICON_SIZE,
                      (first_row + row) * MONKEY_ICON_SIZE, MONKEY_ICON_SIZE, MONKEY_ICON_SIZE,
                      DIMAGE_NONE);
        }
    }
}

static void draw_monkey(int meadow_x, int meadow_y, int type, int x, int y,
                        const bopti_image_t *sprites)
{
    dsubimage(meadow_x + x - MONKEY_SPRITE_SIZE / 2, meadow_y + y - MONKEY_SPRITE_SIZE / 2, sprites,
              (type % 2) * MONKEY_SPRITE_SIZE, (type / 2) * MONKEY_SPRITE_SIZE, MONKEY_SPRITE_SIZE,
              MONKEY_SPRITE_SIZE, DIMAGE_NONE);
}

static int can_place(int x, int y, const struct placed_monkey *placed, int count)
{
    int left = x - MONKEY_SPRITE_SIZE / 2;
    int top = y - MONKEY_SPRITE_SIZE / 2;
    if (left < 0 || top < 0 || left + MONKEY_SPRITE_SIZE > MAP_WIDTH ||
        top + MONKEY_SPRITE_SIZE > MAP_HEIGHT)
        return 0;

    for (int row = top; row < top + MONKEY_SPRITE_SIZE; row++) {
        for (int column = left; column < left + MONKEY_SPRITE_SIZE; column++) {
            if (!(meadow_placement[row * (MAP_WIDTH / 8) + column / 8] & (0x80 >> (column % 8))))
                return 0;
        }
    }

    for (int i = 0; i < count; i++) {
        if (left < placed[i].x + MONKEY_SPRITE_SIZE / 2 &&
            left + MONKEY_SPRITE_SIZE > placed[i].x - MONKEY_SPRITE_SIZE / 2 &&
            top < placed[i].y + MONKEY_SPRITE_SIZE / 2 &&
            top + MONKEY_SPRITE_SIZE > placed[i].y - MONKEY_SPRITE_SIZE / 2) {
            return 0;
        }
    }
    return count < MONKEY_LIMIT;
}

int main(void)
{
    const int meadow_x = (DWIDTH - img_meadow.width) / 2;
    const int meadow_y = (DHEIGHT - img_meadow.height) / 2;
    int selected = 0;
    int first_row = 0;
    int placing = 0;
    int preview_x = 0, preview_y = 0;
    struct placed_monkey placed[MONKEY_LIMIT];
    int placed_count = 0;

    uint16_t red_palette[256];
    bopti_image_t red_sprites = img_monkey_sprites;
    red_sprites.palette = red_palette;
    for (int i = 0; i < img_monkey_sprites.color_count; i++) {
        uint16_t color = img_monkey_sprites.palette[i];
        int red = ((color >> 11) + 3 * 31) / 4;
        int green = ((color >> 5) & 63) / 4;
        int blue = (color & 31) / 4;
        red_palette[i] = (red << 11) | (green << 5) | blue;
    }

    while (1) {
        dclear(C_BLACK);
        dimage(meadow_x, meadow_y, &img_meadow);
        for (int i = 0; i < placed_count; i++) {
            draw_monkey(meadow_x, meadow_y, placed[i].type, placed[i].x, placed[i].y,
                        &img_monkey_sprites);
        }
        draw_selector(meadow_x, meadow_y, first_row);
        int placement_valid = placing && can_place(preview_x, preview_y, placed, placed_count);
        if (placing) {
            draw_monkey(meadow_x, meadow_y, selected, preview_x, preview_y,
                        placement_valid ? &img_monkey_sprites : &red_sprites);
        }
        int focus_x = meadow_x + SELECTOR_X + (selected % 2) * SELECTOR_COLUMN_STEP;
        int focus_y = meadow_y + SELECTOR_Y + (selected / 2 - first_row) * SELECTOR_ROW_STEP;
        drect_border(focus_x - 1, focus_y - 1, focus_x + MONKEY_ICON_SIZE,
                     focus_y + MONKEY_ICON_SIZE, C_NONE, 1, C_WHITE);

        dupdate();

        key_event_t event = getkey_opt(GETKEY_REP_ARROWS, NULL);
        if (event.key == KEY_MENU || (event.key == KEY_EXIT && !placing)) {
            break;
        }

        if (placing) {
            if (event.key == KEY_EXIT)
                placing = 0;
            if (event.key == KEY_EXE && placement_valid) {
                placed[placed_count++] = (struct placed_monkey){preview_x, preview_y, selected};
                placing = 0;
            }
            if (event.key == KEY_LEFT)
                preview_x -= 2;
            if (event.key == KEY_RIGHT)
                preview_x += 2;
            if (event.key == KEY_UP)
                preview_y -= 2;
            if (event.key == KEY_DOWN)
                preview_y += 2;
            if (preview_x < MONKEY_SPRITE_SIZE / 2) {
                preview_x = MONKEY_SPRITE_SIZE / 2;
            }
            if (preview_x > MAP_WIDTH - MONKEY_SPRITE_SIZE / 2) {
                preview_x = MAP_WIDTH - MONKEY_SPRITE_SIZE / 2;
            }
            if (preview_y < MONKEY_SPRITE_SIZE / 2) {
                preview_y = MONKEY_SPRITE_SIZE / 2;
            }
            if (preview_y > MAP_HEIGHT - MONKEY_SPRITE_SIZE / 2) {
                preview_y = MAP_HEIGHT - MONKEY_SPRITE_SIZE / 2;
            }
            continue;
        }

        if (event.key == KEY_EXE) {
            placing = 1;
            preview_x = (5 + 320) / 2;
            preview_y = MAP_HEIGHT / 2;
        }
        if (event.key == KEY_UP && selected >= 2)
            selected -= 2;
        if (event.key == KEY_DOWN && selected + 2 < MONKEY_COUNT)
            selected += 2;
        if (event.key == KEY_LEFT && selected % 2 == 1)
            selected--;
        if (event.key == KEY_RIGHT && selected % 2 == 0)
            selected++;

        int selected_row = selected / 2;
        if (selected_row < first_row)
            first_row = selected_row;
        if (selected_row >= first_row + SELECTOR_VISIBLE_ROWS) {
            first_row = selected_row - SELECTOR_VISIBLE_ROWS + 1;
        }
    }

    return 0;
}
