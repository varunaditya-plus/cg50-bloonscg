#include "game.h"
#include <limits.h>

// Q8 distances and velocity; Q16 degrees for circular flight.
enum { DEG = 65536, REV = 360 * DEG };
static int32_t absolute(int32_t n)
{
    return n < 0 ? -n : n;
}

static int32_t length(int32_t x, int32_t y)
{
    return integer_sqrt(distance_squared(x, y, 0, 0)) * 16;
}

static int32_t sin_phase(int32_t angle)
{
    angle %= REV;
    if (angle < 0)
        angle += REV;
    unsigned whole = angle / DEG, fraction = angle % DEG;
    int a = fixed_sine(whole), b = fixed_sine((whole + 1) % 360);
    return a + (int64_t)(b - a) * fraction / DEG;
}

static int32_t direction(int32_t x, int32_t y)
{
    if (!x && !y)
        return 0;
    int32_t ax = absolute(x), ay = absolute(y);
    unsigned lo = 0, hi = 90;

    while (lo + 1 < hi) {
        unsigned mid = (lo + hi) / 2;
        if ((int64_t)fixed_sine(mid) * ax < (int64_t)fixed_sine(mid + 90) * ay)
            lo = mid;
        else
            hi = mid;
    }
    int64_t low = (int64_t)fixed_sine(lo) * ax - (int64_t)fixed_sine(lo + 90) * ay;
    int64_t high = (int64_t)fixed_sine(hi) * ax - (int64_t)fixed_sine(hi + 90) * ay;
    int32_t angle = lo * DEG;

    if (high > low)
        angle += (int64_t)(-low) * DEG / (high - low);

    if (x < 0)
        angle = 180 * DEG - angle;

    if (y < 0)
        angle = REV - angle;
    return angle % REV;
}

static int32_t turn_towards(int32_t current, int32_t target, int32_t step)
{
    int32_t delta = (target - current) % REV;
    if (delta > REV / 2)
        delta -= REV;
    if (delta < -REV / 2)
        delta += REV;

    if (delta > step)
        delta = step;

    if (delta < -step)
        delta = -step;
    current = (current + delta) % REV;
    return current < 0 ? current + REV : current;
}

static void ace_point(const Tower *t, const AirMovement *m, uint32_t phase, int32_t *x, int32_t *y)
{
    *x = t->x + (int64_t)m->circle_radius * sin_phase(phase + 90 * DEG) / 16384;
    *y = t->y + (int64_t)m->circle_radius * sin_phase(phase) / 16384;
}

static uint32_t takeoff_scale(const Tower *t, const AirMovement *m)
{
    if (!m->takeoff_time || t->air_age >= m->takeoff_time)
        return DEG;
    uint32_t x = (uint64_t)t->air_age * DEG / m->takeoff_time, result = DEG;
    for (unsigned i = 0; i < m->takeoff_exponent / 1000; i++)
        result = (uint64_t)result * x / DEG;

    if (m->takeoff_exponent % 1000 == 500)
        result = (uint64_t)result * integer_sqrt(x * DEG) / DEG;
    return result;
}

static void advance_position(Tower *t)
{
    // Carry each axis remainder through the fixed 50 Hz integration.
    int32_t x = t->air_vx + t->air_remainder_x, y = t->air_vy + t->air_remainder_y;
    t->air_x += x / 50;
    t->air_y += y / 50;
    t->air_remainder_x = x % 50;
    t->air_remainder_y = y % 50;
}

static void ace_tick(Tower *t, const AirMovement *m)
{
    int32_t radius = m->circle_radius;
    if (radius <= 0 || m->speed <= 0)
        return;
    int32_t speed = (int64_t)m->speed * takeoff_scale(t, m) / DEG;
    uint32_t phase_step = (uint64_t)speed * 180 * DEG * 1000000 / ((uint64_t)50 * radius * 3141593);
    t->air_phase = (t->air_phase + phase_step) % REV;
    int32_t x, y;
    ace_point(t, m, t->air_phase, &x, &y);
    int32_t dx = x - t->air_x, dy = y - t->air_y, d = length(dx, dy);
    int32_t catchup = (int64_t)speed * (m->catchup_speed ? m->catchup_speed : 1000) / 1000;

    if (t->air_on_path || d <= catchup / 50 + 16) {
        t->air_vx = dx * 50;
        t->air_vy = dy * 50;
        t->air_x = x;
        t->air_y = y;
        t->air_on_path = 1;
        t->air_angle = direction(dx, dy);
        t->air_remainder_x = t->air_remainder_y = 0;
    } else if (d) {
        int32_t goal = direction(dx, dy);
        if (!t->air_age)
            t->air_angle = goal;
        int32_t turn = (int64_t)m->rotation * DEG / (50 * Q);
        t->air_angle = turn_towards(t->air_angle, goal, turn > 0 ? turn : REV);
        t->air_vx = (int64_t)catchup * sin_phase(t->air_angle + 90 * DEG) / 16384;
        t->air_vy = (int64_t)catchup * sin_phase(t->air_angle) / 16384;
        advance_position(t);
    }
}

static int pursuit_target(const Tower *t, const TowerProfile *p)
{
    int selected = -1;
    int32_t furthest = -1;
    unsigned camo = t->support_camo;
    for (unsigned i = 0; i < p->attack_count; i++)
        camo |= p->attacks[i].camo;

    for (int i = bloon_next(0); i >= 0; i = bloon_next(i + 1)) {
        const Bloon *b = &game.bloons[i];
        if (!b->active || ((b->flags & CAMO) && !camo))
            continue;
        if (b->distance > furthest) {
            selected = i;
            furthest = b->distance;
        }
    }
    return selected;
}

static void heli_tick(Tower *t, const TowerProfile *p, const AirMovement *m)
{
    if (!p->air_pursuit) {
        t->air_x = t->x;
        t->air_y = t->y;
        t->air_vx = t->air_vy = 0;
        t->air_remainder_x = t->air_remainder_y = 0;
        return;
    }
    int32_t x = t->x, y = t->y;
    int target = pursuit_target(t, p);

    if (target >= 0)
        path_position(game.bloons[target].distance + m->pursuit_distance, &x, &y);
    int32_t dx = x - t->air_x, dy = y - t->air_y, d = length(dx, dy), cap = m->speed;

    if (d < m->slowdown_max && m->slowdown_max > 0) {
        int32_t minimum = (int64_t)cap * m->min_velocity / 1000;
        cap = (int64_t)cap * d / m->slowdown_max;
        if (cap < minimum)
            cap = minimum;
    }
    int32_t vx = 0, vy = 0;

    if (d > m->slowdown_min) {
        vx = (int64_t)dx * cap / d;
        vy = (int64_t)dy * cap / d;
    }
    int32_t force = m->force_end;

    if (d >= m->slowdown_max)
        force = m->force_start;

    if ((int64_t)t->air_vx * vx + (int64_t)t->air_vy * vy <= 0)
        force = m->brake_force;
    int32_t change = force / 50, dvx = vx - t->air_vx, dvy = vy - t->air_vy;
    int32_t delta = length(dvx, dvy);

    if (delta > change && delta) {
        dvx = (int64_t)dvx * change / delta;
        dvy = (int64_t)dvy * change / delta;
    }
    t->air_vx += dvx;
    t->air_vy += dvy;
    unsigned repulsed = 0;

    for (unsigned i = 0; i < TOWER_LIMIT; i++) {
        const Tower *other = &game.towers[i];
        if (other == t || !other->active || other->type != 8)
            continue;
        int32_t rx = t->air_x - other->air_x, ry = t->air_y - other->air_y, r = length(rx, ry);
        if (r >= m->repel_radius || m->repel_radius <= 0)
            continue;
        repulsed = 1;
        // Slot order provides a deterministic direction for coincident aircraft.
        if (!r) {
            rx = t > other ? Q : -Q;
            r = Q;
        }
        int32_t push = (int64_t)m->repel_force * (m->repel_radius - r) / (50 * m->repel_radius);
        t->air_vx += (int64_t)rx * push / r;
        t->air_vy += (int64_t)ry * push / r;
    }
    int32_t velocity = length(t->air_vx, t->air_vy);

    if (velocity > m->speed && velocity) {
        t->air_vx = (int64_t)t->air_vx * m->speed / velocity;
        t->air_vy = (int64_t)t->air_vy * m->speed / velocity;
    }

    if (!repulsed &&
        (d <= m->slowdown_min || (d <= length(t->air_vx, t->air_vy) / 50 + 16 &&
                                  (int64_t)dx * t->air_vx + (int64_t)dy * t->air_vy > 0))) {
        t->air_x = x;
        t->air_y = y;
        t->air_vx = t->air_vy = 0;
        t->air_remainder_x = t->air_remainder_y = 0;
    } else {
        advance_position(t);
    }
}

void air_tick(Tower *t, const TowerProfile *p)
{
    if (!p || !p->air_movement || !(p->support & S_AIR))
        return;
    if (t->type == 7)
        ace_tick(t, p->air_movement);
    else if (t->type == 8)
        heli_tick(t, p, p->air_movement);

    if (t->type == 7 && UINT_MAX - t->air_age >= TICK)
        t->air_age += TICK;
}
