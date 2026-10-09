#include "game.h"
#include <stdint.h>

static uint64_t root64(uint64_t value)
{
    if (value <= UINT32_MAX)
        return integer_sqrt(value);

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

static int32_t scaled(int64_t value, unsigned shift)
{
    // Signed division rounds towards zero; an arithmetic shift needs this bias.
    if (value < 0)
        value += ((int64_t)1 << shift) - 1;
    return value >> shift;
}

static uint32_t contact_fraction(uint32_t numerator, uint32_t denominator)
{
    if (numerator == denominator)
        return 65536;

    uint32_t fraction = 0;
    for (unsigned bit = 0; bit < 16; bit++) {
        numerator <<= 1;
        fraction <<= 1;
        if (numerator >= denominator) {
            numerator -= denominator;
            fraction++;
        }
    }
    return fraction;
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

    int32_t sx, sy, ox, oy, radius_scaled;
    if (largest <= 524287) {
        sx = (int32_t)dx / 16;
        sy = (int32_t)dy / 16;
        ox = (int32_t)fx / 16;
        oy = (int32_t)fy / 16;
        radius_scaled = (int32_t)r / 16;
    } else {
        while ((largest >> shift) > 32767)
            shift++;
        sx = scaled(dx, shift);
        sy = scaled(dy, shift);
        ox = scaled(fx, shift);
        oy = scaled(fy, shift);
        radius_scaled = scaled(r, shift);
    }

    if ((ox > radius_scaled && ox + sx > radius_scaled) ||
        (ox < -radius_scaled && ox + sx < -radius_scaled) ||
        (oy > radius_scaled && oy + sy > radius_scaled) ||
        (oy < -radius_scaled && oy + sy < -radius_scaled))
        return -1;

    int32_t a = sx * sx + sy * sy;
    int32_t dot = ox * sx + oy * sy;
    int32_t c = ox * ox + oy * oy - radius_scaled * radius_scaled;

    if (c <= 0)
        return 0;

    if (!a || dot >= 0)
        return -1;
    int64_t discriminant = (int64_t)dot * dot - (int64_t)a * c;

    if (discriminant < 0)
        return -1;
    int64_t numerator = -(int64_t)dot - (int64_t)root64(discriminant);

    if (numerator < 0)
        numerator = 0;

    if (numerator > a)
        return -1;
    return contact_fraction(numerator, a);
}

int collision_area_contains(const AttackDef *a, int32_t x, int32_t y, const Bloon *b)
{
    unsigned radius = a->area ? a->area : a->radius;
    unsigned outer = (radius + bloon_defs[b->type].radius) / 16;
    int32_t distance = distance_squared(x, y, b->x, b->y);
    if (distance > (int32_t)outer * (int32_t)outer)
        return 0;

    unsigned inner = radius * a->inner_radius / 16000;
    return !inner || distance >= (int32_t)inner * (int32_t)inner;
}
