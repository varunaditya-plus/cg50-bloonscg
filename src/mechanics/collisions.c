#include "game.h"
#include <stdint.h>

static uint64_t root64(uint64_t value)
{
    uint64_t result = 0, bit = (uint64_t)1 << 62;
    while (bit > value)
        bit >>= 2;
    while (bit) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else
            result >>= 1;
        bit >>= 2;
    }
    return result;
}

int32_t collision_contact_fraction(const Shot *s, int32_t nx, int32_t ny, const Bloon *b,
                                   unsigned radius)
{

    // Scale the segment-circle quadratic to keep its discriminant within int64.
    int64_t dx = (int64_t)nx - s->x, dy = (int64_t)ny - s->y;
    int64_t fx = (int64_t)s->x - b->x, fy = (int64_t)s->y - b->y, r = radius;
    unsigned shift = 4;
    int64_t largest = dx < 0 ? -dx : dx;
    int64_t values[] = {dy, fx, fy, r};

    for (unsigned n = 0; n < 4; n++) {
        int64_t v = values[n] < 0 ? -values[n] : values[n];
        if (v > largest)
            largest = v;
    }

    while ((largest >> shift) > 32767)
        shift++;
    int64_t scale = (int64_t)1 << shift;
    dx /= scale;
    dy /= scale;
    fx /= scale;
    fy /= scale;
    r /= scale;
    int64_t a = dx * dx + dy * dy, dot = fx * dx + fy * dy, c = fx * fx + fy * fy - r * r;

    if (c <= 0)
        return 0;

    if (!a || dot >= 0)
        return -1;
    int64_t discriminant = dot * dot - a * c;

    if (discriminant < 0)
        return -1;
    int64_t numerator = -dot - (int64_t)root64(discriminant);

    if (numerator < 0)
        numerator = 0;

    if (numerator > a)
        return -1;
    return (uint64_t)numerator * 65536 / a;
}

int collision_area_contains(const AttackDef *a, int32_t x, int32_t y, const Bloon *b)
{
    unsigned radius = a->area ? a->area : a->radius;
    unsigned outer = (radius + bloon_defs[b->type].radius) / 16;
    int32_t distance = distance_squared(x, y, b->x, b->y);
    if (distance > (int32_t)outer * (int32_t)outer)
        return 0;

    unsigned inner = (uint64_t)radius * a->inner_radius / 1000 / 16;
    return !inner || distance >= (int32_t)inner * (int32_t)inner;
}
