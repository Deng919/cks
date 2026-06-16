#ifndef APP_MATH_H
#define APP_MATH_H

#include <stdint.h>
#include "app_types.h"

/* 64 位中间值写回 32 位业务字段时用饱和转换 */
uint32_t app_u32_from_u64(uint64_t value);

/* 坐标差值常用的无符号绝对值 */
uint32_t app_abs_u32(int32_t value);

/* 整数平方根，避免在 MCU 上引入 sqrt()/浮点库 */
uint32_t app_isqrt_u64(uint64_t value);

/* 两点欧氏距离，单位 mm；参数为空时返回 0 */
uint32_t app_point_distance_mm(const app_point_t *from, const app_point_t *to);

/* 闭区间限幅 */
int32_t app_clamp_i32(int32_t value, int32_t min_value, int32_t max_value);

/* 解析纯数字 uint16，并校验闭区间范围；失败时不改 value */
uint8_t app_parse_u16_bounded(const char *text, uint16_t min_value, uint16_t max_value, uint16_t *value);

#endif
