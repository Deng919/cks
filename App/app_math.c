#include "app_math.h"

/* 64 位中间值写回 32 位字段时使用饱和值 */
uint32_t app_u32_from_u64(uint64_t value)
{
    if (value > 0xFFFFFFFFULL) {
        return 0xFFFFFFFFU;
    }

    return (uint32_t)value;
}

/* 坐标差值的无符号绝对值 */
uint32_t app_abs_u32(int32_t value)
{
    if (value < 0) {
        return (uint32_t)(-value);
    }

    return (uint32_t)value;
}

/* 整数平方根，给距离计算用 */
uint32_t app_isqrt_u64(uint64_t value)
{
    uint64_t bit;
    uint64_t result;

    result = 0ULL;
    bit = 1ULL << 62;
    while (bit > value) {
        bit >>= 2;
    }

    while (bit != 0ULL) {
        if (value >= (result + bit)) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }

    return app_u32_from_u64(result);
}

/* 两个业务坐标点之间的距离，单位 mm */
uint32_t app_point_distance_mm(const app_point_t *from, const app_point_t *to)
{
    uint32_t dx;
    uint32_t dy;

    if ((from == 0) || (to == 0)) {
        return 0U;
    }

    dx = app_abs_u32(to->x_mm - from->x_mm);
    dy = app_abs_u32(to->y_mm - from->y_mm);
    return app_isqrt_u64(((uint64_t)dx * (uint64_t)dx) + ((uint64_t)dy * (uint64_t)dy));
}

/* 闭区间限幅 */
int32_t app_clamp_i32(int32_t value, int32_t min_value, int32_t max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }

    return value;
}

/* 解析有界 uint16，避免 UI 输入在转换时回绕 */
uint8_t app_parse_u16_bounded(const char *text, uint16_t min_value, uint16_t max_value, uint16_t *value)
{
    uint32_t parsed;

    if ((text == 0) || (value == 0) || (min_value > max_value) || (*text == '\0')) {
        return 0U;
    }

    parsed = 0U;
    while (*text != '\0') {
        if ((*text < '0') || (*text > '9')) {
            return 0U;
        }
        parsed = (parsed * 10U) + (uint32_t)(*text - '0');
        if (parsed > max_value) {
            return 0U;
        }
        text++;
    }

    if (parsed < min_value) {
        return 0U;
    }

    *value = (uint16_t)parsed;
    return 1U;
}
