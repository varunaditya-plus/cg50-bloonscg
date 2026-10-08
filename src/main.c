#include <gint/display.h>
#include <gint/keyboard.h>

// meadow-guide.png marks grass at x=5..326; exclude paths, trees and UI edges.
extern const bopti_image_t img_meadow;

int main(void)
{
    const int meadow_x = (DWIDTH - img_meadow.width) / 2;
    const int meadow_y = (DHEIGHT - img_meadow.height) / 2;

    while (1) {
        dclear(C_BLACK);
        dimage(meadow_x, meadow_y, &img_meadow);

        dupdate();

        pollevent();
        if (keydown(KEY_EXIT) || keydown(KEY_MENU)) {
            break;
        }
    }

    return 0;
}
