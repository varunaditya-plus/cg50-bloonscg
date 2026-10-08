#include <gint/display.h>
#include <gint/keyboard.h>

extern const bopti_image_t img_meadow;
// Portraits: https://www.spriters-resource.com/pc_computer/bloonstd6/
extern const bopti_image_t img_monkey_portraits;

enum {
    MONKEY_COUNT = 20,
    MONKEY_ICON_SIZE = 24,
    SELECTOR_X = 324,
    SELECTOR_Y = 26,
    SELECTOR_COLUMN_STEP = 29,
    SELECTOR_ROW_STEP = 31,
    SELECTOR_VISIBLE_ROWS = 6,
};

static void draw_selector(int meadow_x, int meadow_y, int selected, int first_row)
{

    for (int row = 0; row < SELECTOR_VISIBLE_ROWS; row++) {
        for (int column = 0; column < 2; column++) {
            int monkey = (first_row + row) * 2 + column;

            int x = meadow_x + SELECTOR_X + column * SELECTOR_COLUMN_STEP;
            int y = meadow_y + SELECTOR_Y + row * SELECTOR_ROW_STEP;
            dsubimage(x, y, &img_monkey_portraits, column * MONKEY_ICON_SIZE,
                      (first_row + row) * MONKEY_ICON_SIZE, MONKEY_ICON_SIZE, MONKEY_ICON_SIZE,
                      DIMAGE_NONE);
            if (monkey == selected) {
                drect_border(x - 1, y - 1, x + MONKEY_ICON_SIZE, y + MONKEY_ICON_SIZE, C_NONE, 1,
                             C_WHITE);
            }
        }
    }
}

int main(void)
{
    const int meadow_x = (DWIDTH - img_meadow.width) / 2;
    const int meadow_y = (DHEIGHT - img_meadow.height) / 2;
    int selected = 0;
    int first_row = 0;

    while (1) {
        dclear(C_BLACK);
        dimage(meadow_x, meadow_y, &img_meadow);
        draw_selector(meadow_x, meadow_y, selected, first_row);

        dupdate();

        key_event_t event = getkey_opt(GETKEY_REP_ARROWS, NULL);
        if (event.key == KEY_EXIT || event.key == KEY_MENU) {
            break;
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
