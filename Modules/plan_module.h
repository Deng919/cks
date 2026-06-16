#ifndef PLAN_MODULE_H
#define PLAN_MODULE_H

#include "app_types.h"

/*
 * 路径规划模块
 * 每个 sub_batch 生成一条从服务点出发并返回的闭环航线
 */

typedef struct
{
    /* 每桶任务和喷洒区域 */
    const mix_output_t *mix_output;

    /* 农田边界 */
    app_field_t field;

    /* 起飞、续液、换液和返航位置 */
    app_point_t service_point;

    /* 服务点到农田边界的最小距离 */
    uint16_t service_safe_distance_mm;

    /* S 型航线的行距由喷幅和重叠率决定 */
    uint16_t spray_width_mm;
    uint16_t spray_overlap_rate_x100;

    /* 飞行耗时估算用 */
    uint32_t flight_speed_mmps;

    /* 药箱容量，单位 0.1 ml */
    uint32_t tank_capacity_ml_x10;
} plan_input_t;

/* 当前无内部状态，保留生命周期接口 */
void plan_module_init(void);

/* 把配药桶任务转换为全局航点数组 */
app_result_t plan_module_run(const plan_input_t *input, plan_output_t *output);

#endif
