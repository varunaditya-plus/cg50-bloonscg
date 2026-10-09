#include "game.h"

static int32_t clamp(int32_t n, int32_t lo, int32_t hi)
{
    return n < lo ? lo : n > hi ? hi : n;
}

static int32_t sine_q8(int32_t angle)
{
    angle = (angle % (360 * Q) + 360 * Q) % (360 * Q);
    int32_t degree = angle / Q, fraction = angle % Q;
    return fixed_sine(degree) +
           (int32_t)(fixed_sine(degree + 1) - fixed_sine(degree)) * fraction / Q;
}

static int32_t angle_q8(int32_t x, int32_t y)
{
    if (!x && !y)
        return 0;
    int64_t ax = x < 0 ? -(int64_t)x : x, ay = y < 0 ? -(int64_t)y : y;
    unsigned lo = 0, hi = 90;

    while (hi - lo > 1) {
        unsigned mid = (lo + hi) / 2;
        if (ay * fixed_sine(90 - mid) > ax * fixed_sine(mid))
            lo = mid;
        else
            hi = mid;
    }
    int64_t a = ay * fixed_sine(90 - lo) - ax * fixed_sine(lo);
    int64_t b = ax * fixed_sine(hi) - ay * fixed_sine(90 - hi);
    int32_t angle = lo * Q + (a + b ? (a * Q / (a + b)) : 0);

    if (x < 0)
        angle = 180 * Q - angle;

    if (y < 0)
        angle = -angle;
    return angle;
}

static void velocity(Shot *s, int32_t angle, int32_t speed)
{
    s->vx = (int64_t)sine_q8(angle + 90 * Q) * speed / 16384;
    s->vy = (int64_t)sine_q8(angle) * speed / 16384;
}

static int homing_target(const Shot *s)
{
    const AttackDef *a = s->attack;
    unsigned old = s->target;
    if (old < BLOON_LIMIT && game.bloons[old].active && !shot_history_contains(s, old) &&
        !(a->homing_flags & 2))
        return old;

    if (old < BLOON_LIMIT && !(a->homing_flags & 1) && a->homing_kind != 2)
        return -1;
    int target = -1;
    int32_t best = INT32_MAX;

    for (int n = bloon_next(0); n >= 0; n = bloon_next(n + 1)) {
        const Bloon *b = &game.bloons[n];
        if (!b->active || shot_history_contains(s, n) || ((b->flags & CAMO) && !s->camo))
            continue;
        if ((a->flags & A_MOAB_ONLY) && b->type < MOAB)
            continue;
        if ((a->flags & A_NORMAL_ONLY) && b->type >= MOAB)
            continue;
        if ((a->target_flags & AT_NO_MOAB) && b->type >= MOAB)
            continue;
        if ((a->target_flags & AT_NO_FROZEN) && b->freeze)
            continue;
        if ((a->target_flags & AT_NO_LEAD) && b->type == LEAD)
            continue;
        if ((a->target_flags & AT_NO_WIND) && b->wind_remaining)
            continue;
        int32_t d = distance_squared(s->x, s->y, b->x, b->y);
        if (a->homing_range != 65535 &&
            d > (int32_t)(a->homing_range / 16) * (a->homing_range / 16))
            continue;
        if (d < best) {
            best = d;
            target = n;
        }
    }
    return target;
}

static void home(Shot *s)
{
    const AttackDef *a = s->attack;
    int target = homing_target(s);
    int32_t heading = angle_q8(s->vx, s->vy);
    int32_t speed = integer_sqrt(distance_squared(0, 0, s->vx, s->vy)) * 16;

    if (!speed)
        speed = a->speed;
    int32_t turn = a->homing_turn;

    if (a->homing_kind == 2)
        turn *= 60;
    else
        turn += (int64_t)a->homing_turn_acceleration * s->age / 6000;

    if (a->homing_max_turn && turn > a->homing_max_turn)
        turn = a->homing_max_turn;
    int32_t delta = 0;

    if (target >= 0) {
        s->target = target;
        delta = angle_q8(game.bloons[target].x - s->x, game.bloons[target].y - s->y) - heading;
        delta = (delta + 540 * Q) % (360 * Q) - 180 * Q;
        if (!(a->homing_flags & 4) && a->homing_seek_angle != 65535 &&
            (delta < 0 ? -delta : delta) > a->homing_seek_angle)
            target = -1;
    }
    int32_t absolute = delta < 0 ? -delta : delta;

    if (a->homing_kind == 2) {
        if (target >= 0 && absolute <= a->homing_accelerate_angle)
            speed += (int64_t)a->homing_acceleration * TICK / 6000;
        else if (target < 0 || absolute > a->homing_decelerate_angle)
            speed -= (int64_t)a->homing_min_speed * TICK / 6000;
    } else
        speed += (int64_t)a->homing_acceleration * TICK / 6000;

    if (a->homing_min_speed && speed < a->homing_min_speed)
        speed = a->homing_min_speed;

    if (a->homing_max_speed && speed > a->homing_max_speed)
        speed = a->homing_max_speed;

    if (target >= 0)
        heading += clamp(delta, -(int64_t)turn * TICK / 6000, (int64_t)turn * TICK / 6000);
    velocity(s, heading, speed);
}

static int segment_interval(unsigned n, int32_t x, int32_t y, unsigned radius, unsigned *begin,
                            unsigned *end)
{
    const PathPoint *p = &meadow_path[n], *next = p + 1;
    int32_t dx = next->x - p->x, dy = next->y - p->y;
    // Clip path segments to the range circle for length-weighted sampling.
    int64_t squared = (int64_t)dx * dx + (int64_t)dy * dy;
    unsigned length = next->distance - p->distance;

    if (!length || !squared)
        return 0;
    int64_t dot = (int64_t)(x - p->x) * dx + (int64_t)(y - p->y) * dy;
    int32_t fraction = dot * 65536 / squared;
    int32_t cx = p->x + (int64_t)dx * fraction / 65536, cy = p->y + (int64_t)dy * fraction / 65536;
    int32_t perpendicular = distance_squared(x, y, cx, cy);
    int32_t r = radius / 16;

    if (perpendicular > r * r)
        return 0;
    int32_t half = integer_sqrt(r * r - perpendicular) * 16;
    int32_t middle = (int64_t)length * fraction / 65536;
    int32_t low = clamp(middle - half, 0, length), high = clamp(middle + half, 0, length);

    if (low >= high)
        return 0;
    *begin = low;
    *end = high;
    return 1;
}

static int track_point(int32_t x, int32_t y, unsigned range, int closest, int32_t *px, int32_t *py)
{
    uint32_t available = 0;
    int32_t best = INT32_MAX, bx = 0, by = 0;
    for (unsigned n = 0; n + 1 < meadow_path_count; n++) {
        unsigned low, high;
        if (!segment_interval(n, x, y, range, &low, &high))
            continue;
        available += high - low;
        const PathPoint *p = &meadow_path[n], *next = p + 1;
        int32_t dx = next->x - p->x, dy = next->y - p->y;
        int64_t squared = (int64_t)dx * dx + (int64_t)dy * dy;
        int32_t fraction = clamp(
            ((int64_t)(x - p->x) * dx + (int64_t)(y - p->y) * dy) * 65536 / squared, 0, 65536);
        int32_t xx = p->x + (int64_t)dx * fraction / 65536,
                yy = p->y + (int64_t)dy * fraction / 65536;
        int32_t d = distance_squared(x, y, xx, yy);
        if (d < best) {
            best = d;
            bx = xx;
            by = yy;
        }
    }

    if (!available)
        return 0;

    if (closest) {
        *px = bx;
        *py = by;
        return 1;
    }
    unsigned selection = game_random() % available;

    for (unsigned n = 0; n + 1 < meadow_path_count; n++) {
        unsigned low, high;
        if (!segment_interval(n, x, y, range, &low, &high))
            continue;
        if (selection >= high - low) {
            selection -= high - low;
            continue;
        }
        const PathPoint *p = &meadow_path[n], *next = p + 1;
        unsigned distance = low + selection, length = next->distance - p->distance;
        *px = p->x + (int64_t)(next->x - p->x) * distance / length;
        *py = p->y + (int64_t)(next->y - p->y) * distance / length;
        return 1;
    }
    return 0;
}

static void scatter(int32_t *x, int32_t *y, unsigned radius)
{
    if (!radius)
        return;

    // sqrt(U) distributes scatter evenly over the disk rather than its radius.
    unsigned r = (uint64_t)radius * integer_sqrt(game_random() & 65535) / 255;
    unsigned angle = game_random() % 360;
    *x += (int64_t)r * fixed_sine(angle + 90) / 16384;
    *y += (int64_t)r * fixed_sine(angle) / 16384;
}

int projectile_setup(Shot *s, const AttackDef *a, unsigned owner, int target)
{
    s->x = s->origin_x;
    s->y = s->origin_y;
    int32_t x = target >= 0 ? game.bloons[target].x : game.towers[owner].aim_x;
    int32_t y = target >= 0 ? game.bloons[target].y : game.towers[owner].aim_y;

    if (a->target_kind == 3 || a->fixed_target) {
        x = game.towers[owner].aim_x;
        y = game.towers[owner].aim_y;
    }

    if (a->trigger == TR_MAIN && (a->flags & A_MORTAR)) {
        x = game.towers[owner].aim_x;
        y = game.towers[owner].aim_y;
        scatter(&x, &y, a->target_spread);
        s->x = x;
        s->y = y;
        s->destination_x = x;
        s->destination_y = y;
        s->vx = s->vy = 0;
        return 1;
    }

    if (a->trigger == TR_MAIN && (a->flags & (A_SPIKE | A_WALL | A_FOAM | A_TRAP))) {
        AttackDef effective;
        unsigned speed;
        support_attack(owner, a, &effective, &speed);
        if (a->flags & A_SPIKE) {
            if (!track_point(s->origin_x, s->origin_y, effective.range, 0, &x, &y))
                return 0;
        } else if (a->target_kind == 9 ||
                   (!(a->fixed_target) && (a->flags & (A_WALL | A_FOAM | A_TRAP)))) {
            if (!track_point(s->origin_x, s->origin_y, effective.range, 1, &x, &y))
                return 0;
        }
        scatter(&x, &y, a->target_spread ? a->target_spread : a->target_track_offset);
        s->destination_x = x;
        s->destination_y = y;
        if (a->curve_kind != 2) {
            s->x = x;
            s->y = y;
            s->vx = s->vy = 0;
            return 1;
        }
    } else {
        s->destination_x = x;
        s->destination_y = y;
    }
    unsigned distance = integer_sqrt(distance_squared(x, y, s->origin_x, s->origin_y)) * 16;

    if (!distance)
        distance = 1;
    s->dir_x = (int64_t)(x - s->origin_x) * 16384 / distance;
    s->dir_y = (int64_t)(y - s->origin_y) * 16384 / distance;

    if (a->curve_kind == 2) {
        s->curve_time = a->curve_time ? a->curve_time
                        : a->speed    ? (uint64_t)distance * 6000 / a->speed
                                      : TICK;
        if (!s->curve_time)
            s->curve_time = TICK;
        if (a->motion_flags & 4)
            if (s->curve_time < s->life)
                s->life = s->curve_time;
    } else
        s->curve_time = a->curve_time;

    if (a->random_spread) {
        int32_t angle = angle_q8(s->vx, s->vy);
        int32_t offset = (int32_t)(game_random() % (a->random_spread + 1)) - a->random_spread / 2;
        velocity(s, angle + (int64_t)offset * Q / 1000, a->speed);
    }
    return 1;
}

static int32_t sampled(const AttackCurvePoint *p, unsigned count, unsigned fraction, int y)
{
    if (!count)
        return 0;
    unsigned position = (uint64_t)fraction * (count - 1), index = position / 65535,
             remainder = position % 65535;
    int32_t a = y ? p[index].y : p[index].x;

    if (index + 1 < count) {
        int32_t b = y ? p[index + 1].y : p[index + 1].x;
        a += (int64_t)(b - a) * remainder / 65535;
    }
    return a;
}

int projectile_step(Shot *s, int32_t *nx, int32_t *ny)
{
    const AttackDef *a = s->attack;
    if (a->refresh_pierce) {
        unsigned interval = a->refresh_pierce;
        if (a->motion_flags & 128) {
            AttackDef effective;
            unsigned rate;
            support_attack(s->owner, a, &effective, &rate);
            interval = (uint64_t)interval * 1000 / rate;
        }
        if (!interval)
            interval = 1;
        if (s->age - s->pierce_clock >= interval) {
            s->pierce_clock += ((s->age - s->pierce_clock) / interval) * interval;
            s->pierce = (a->pierce + s->brew_pierce) * s->pierce_factor / 1000;
        }
    }

    if (a->flags & A_HOMING)
        home(s);
    *nx = s->x + s->vx / 50;
    *ny = s->y + s->vy / 50;

    if (a->curve_kind == 2 && a->curve_count > 1) {
        unsigned duration = s->curve_time ? s->curve_time : TICK;
        unsigned fraction = (uint64_t)s->age * 65535 / duration;
        if (fraction > 65535)
            fraction = 65535;
        int32_t progress = sampled(a->curve, a->curve_count, fraction, 1);
        if ((a->motion_flags & 16) && s->target < BLOON_LIMIT && game.bloons[s->target].active) {
            s->destination_x = game.bloons[s->target].x;
            s->destination_y = game.bloons[s->target].y;
        }
        *nx = s->origin_x + (int64_t)(s->destination_x - s->origin_x) * progress / Q;
        *ny = s->origin_y + (int64_t)(s->destination_y - s->origin_y) * progress / Q;
        if (s->age >= duration && (a->motion_flags & 8))
            s->vx = s->vy = 0;
        if (s->age >= duration && (a->motion_flags & 4))
            s->life = s->age;
    } else if (a->curve_kind == 1 && a->curve_count > 1) {
        uint32_t distance = (uint64_t)s->age * a->speed / 6000;
        if (distance > a->curve_length)
            distance = a->curve_length;

        if (a->ease_count > 1) {
            unsigned fraction = (uint64_t)s->age * 65535 / (s->curve_time ? s->curve_time : 1);
            if (fraction > 65535)
                fraction = 65535;
            distance = (uint64_t)a->curve_length * sampled(a->ease, a->ease_count, fraction, 0) /
                       (100 * Q);
        }
        unsigned index = 0, begin = 0, length = 0;
        while (index + 1 < a->curve_count && a->curve_distances[index + 1] <= distance)
            index++;
        begin = a->curve_distances[index];
        if (index + 1 < a->curve_count)
            length = a->curve_distances[index + 1] - begin;
        int32_t x = a->curve[index].x, y = a->curve[index].y;
        if (index + 1 < a->curve_count) {
            if (length) {
                x += (int64_t)(a->curve[index + 1].x - x) * (distance - begin) / length;
                y += (int64_t)(a->curve[index + 1].y - y) * (distance - begin) / length;
            }
        }
        *nx = s->origin_x + ((int64_t)y * s->dir_x + (int64_t)x * s->dir_y) / 16384;
        *ny = s->origin_y + ((int64_t)y * s->dir_y - (int64_t)x * s->dir_x) / 16384;
        if (s->age >= s->curve_time)
            s->life = s->age;
    }

    if (a->curve_kind == 2 && (a->motion_flags & 2) && s->age < s->curve_time)
        return 0;

    if (a->collision_interval) {
        if (s->age - s->collision_clock < a->collision_interval)
            return 0;
        s->collision_clock +=
            ((s->age - s->collision_clock) / a->collision_interval) * a->collision_interval;
    }
    return s->pierce && (a->motion_flags & 64);
}

int projectile_keep_on_zero(const AttackDef *a)
{
    return (a->motion_flags & 32) != 0;
}

int projectile_can_hit(const Shot *s, unsigned enemy)
{
    if (s->attack->target_kind == 7 && s->target != enemy)
        return 0;
    if ((s->attack->target_flags & AT_NO_WIND) && game.bloons[enemy].wind_remaining)
        return 0;

    for (unsigned n = 0; n < s->attack->effect_count; n++) {
        const AttackEffect *e = &s->attack->effects[n];
        if (e->kind == EF_PIERCE_COST && (e->modifier_flags & 1) &&
            s->pierce < projectile_pierce_cost(s->attack, enemy))
            return -1;
    }
    return 1;
}

unsigned projectile_pierce_cost(const AttackDef *a, unsigned enemy)
{
    const Bloon *b = &game.bloons[enemy];
    unsigned tags = 0, cost = 1;
    if (b->type >= MOAB)
        tags |= TAG_MOABS;

    if (b->type == CERAMIC)
        tags |= TAG_CERAMIC;

    if (b->flags & FORTIFIED)
        tags |= TAG_FORTIFIED;

    if (b->flags & CAMO)
        tags |= TAG_CAMO;

    if (b->type == LEAD)
        tags |= TAG_LEAD;

    if (b->type == BLACK)
        tags |= TAG_BLACK;

    if (b->type == WHITE)
        tags |= TAG_WHITE;

    if (b->type == ZEBRA)
        tags |= TAG_ZEBRA;

    if (b->type == MOAB)
        tags |= TAG_MOAB;

    if (b->type == BFB)
        tags |= TAG_BFB;

    for (unsigned n = 0; n < a->effect_count; n++) {
        const AttackEffect *e = &a->effects[n];
        if (e->kind != EF_PIERCE_COST || (e->tags && !(e->tags & tags)) || (e->exclude_tags & tags))
            continue;
        if (e->value > 0)
            cost += e->value;
    }
    return cost;
}

void projectile_round_end(void)
{

    for (unsigned n = 0; n < game.shot_count;) {
        Shot *s = &game.shots[game.shot_active[n]];
        if ((s->attack->flags & A_SPIKE) && !s->attack->expire_rounds)
            shot_release(s);
        else
            n++;
    }
}

int projectile_wind_apply(Bloon *b, const AttackEffect *e, unsigned immunity)
{
    if (b->type >= MOAB && !(e->modifier_flags & 128))
        return 0;
    unsigned properties = bloon_defs[b->type].immunity | (b->freeze ? IMM_FROZEN : 0);
    if ((properties & ~b->property_strip) & immunity)
        return 0;
    int32_t distance = e->value, maximum = e->auxiliary[0];

    if (maximum > distance)
        distance += game_random() % (maximum - distance + 1);
    unsigned tag = b->type == CERAMIC ? TAG_CERAMIC : b->type >= MOAB ? TAG_MOABS : 0;

    if (e->auxiliary[2] && (e->tags & tag))
        distance = (int64_t)distance * e->auxiliary[2] / 1000;

    if (distance > b->distance)
        distance = b->distance;
    b->wind_remaining = distance;
    b->wind_multiplier = e->auxiliary[3] ? e->auxiliary[3] : 1000;
    return distance > 0;
}

int projectile_wind_step(Bloon *b)
{
    if (!b->wind_remaining)
        return 0;

    unsigned speed = (uint64_t)bloon_defs[b->type].speed * b->wind_multiplier / 1000;
    unsigned step = (speed + b->move_remainder) / 50;
    b->move_remainder = (speed + b->move_remainder) % 50;

    if (step > (unsigned)b->wind_remaining)
        step = b->wind_remaining;
    b->distance -= step;
    b->wind_remaining -= step;

    if (b->distance < 0) {
        b->distance = 0;
        b->wind_remaining = 0;
    }
    return 1;
}

int projectile_child_ready(Shot *s, const AttackDef *child)
{

    unsigned interval = child->interval;
    if (child->trigger == TR_INTERVAL && !interval)
        interval = child->period;

    if (!interval)
        return 1;

    if (s->child_clock && s->age < s->child_clock)
        return 0;
    s->child_clock = s->age + interval;
    return 1;
}

void projectile_emission(Shot *s, const AttackDef *a, unsigned index)
{
    if (a->emission_kind != 1)
        return;
    unsigned count = a->count ? a->count : 1;
    int32_t offset = a->emission_start;

    if (count > 1)
        offset += (int32_t)a->emission_span * index / (count - 1);
    int32_t dx = s->dir_x, dy = s->dir_y;
    s->origin_x += ((int64_t)offset * dy + (int64_t)a->emission_y * dx) / 16384;
    s->origin_y += ((int64_t)-offset * dx + (int64_t)a->emission_y * dy) / 16384;
    s->x = s->origin_x;
    s->y = s->origin_y;
}
