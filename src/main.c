#include "mechanics/game.h"
#include <gint/gint.h>
#include <gint/keyboard.h>
#include <gint/timer.h>

static volatile int wake;
static volatile unsigned ticks;

static int tick(void)
{
    ticks++;
    wake = 1;
    return TIMER_CONTINUE;
}

int main(void)
{
    // https://www.planet-casio.com/Fr/forums/topic18535-1-gint-programming-questions.html
    gint_setrestart(1);
    game_init();
    ui_init();

    int timer = timer_configure(TIMER_ANY, 20000, GINT_CALL(tick));
    if (timer < 0) {
        return 1;
    }

    timer_start(timer);
    unsigned processed = 0;
    int active = 1;

    while (active) {
        unsigned now = ticks;
        while (processed < now) {
            for (unsigned speed = 0; speed < game.speed; speed++) {
                game_tick();
            }
            processed++;
        }

        ui_draw();
        // A pending tick skips waiting, while getkey still reads queued key events.
        wake = (ticks != processed);
        key_event_t event = getkey_opt(GETKEY_REP_ARROWS, &wake);
        if (event.type != KEYEV_NONE) {
            active = ui_key(event.key);
        }
    }

    timer_stop(timer);
    return 0;
}
