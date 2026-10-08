#include <gint/display.h>
#include <gint/keyboard.h>

int main(void)
{

    while (1) {
        dclear(C_WHITE);

        dtext(0, 0, C_BLACK, "Hello World");

        dupdate();

        pollevent();
        if (keydown(KEY_EXIT) || keydown(KEY_MENU)) {
            break;
        }
    }

    return 0;
}
