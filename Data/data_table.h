#ifndef DATA_TABLE_H
#define DATA_TABLE_H

#include "app_types.h"

/*
 * 默认参数和规则表集中放这里
 * 改演示场景时尽量改数据表，不改算法逻辑
 */

typedef struct
{
    /* 预留的作物环境和基础用药参数 */
    uint8_t crop_type;
    uint8_t growth_stage;
    uint16_t temp_low_x10;
    uint16_t temp_high_x10;
    uint16_t humidity_low_x10;
    uint16_t humidity_high_x10;
    uint32_t light_low_lux;
    uint32_t light_high_lux;
    uint16_t max_wind_x10;
    uint16_t base_dose_ml_per_mu_x10;
    uint8_t pest_weight;
    uint8_t disease_weight;
} crop_rule_t;

typedef struct
{
    /* 自动检测的地块比例和病害权重 */
    uint8_t healthy_min_pct;
    uint8_t healthy_max_pct;
    uint8_t water_min_pct;
    uint8_t water_max_pct;
    uint8_t disease_weight[APP_MAX_DISEASE_TYPE_COUNT];
} monitor_auto_profile_t;

typedef struct
{
    /* 默认农药 ID 和浓度 */
    uint8_t pesticide_id;
    uint16_t default_concentration_x1000;
} pesticide_rule_t;

typedef struct
{
    /*
     * 单位面积配方
     * water_ml_per_m2_x100 和 pesticide_ml_per_m2_x100 都是 ml/m2 x100
     */
    spray_content_t spray_content;
    disease_type_t disease_type;
    uint8_t pesticide_id;
    uint16_t dilution_ratio_x100;
    uint32_t water_ml_per_m2_x100;
    uint32_t pesticide_ml_per_m2_x100;
} mix_recipe_t;

typedef struct
{
    /* 喷幅，单位 mm */
    uint16_t spray_width_mm;
    /* 飞行速度，单位 mm/s */
    uint32_t flight_speed_mmps;
    /* 药箱容量，单位 0.1 ml */
    uint32_t tank_capacity_ml_x10;
} drone_rule_t;

typedef struct
{
    /* 默认规划参数 */
    uint16_t service_safe_distance_mm;
    uint16_t spray_overlap_rate_x100;
} plan_rule_t;

/* 作物规则 */
extern const crop_rule_t g_data_default_crop;
extern const crop_rule_t g_data_crop_rule_table[];
extern const uint16_t g_data_crop_rule_count;
extern const environment_input_t g_data_default_environment;

/* 农田网格和自动检测分布 */
extern const monitor_grid_t g_data_default_monitor_grid;
extern const monitor_auto_profile_t g_data_monitor_auto_profile;

/* 农药和配药配方 */
extern const pesticide_rule_t g_data_default_pesticide;
extern const mix_recipe_t g_data_mix_recipe_table[];
extern const uint16_t g_data_mix_recipe_count;

/* UI 药箱容量选项，单位 L */
extern const uint16_t g_data_tank_capacity_l_table[];
extern const uint16_t g_data_tank_capacity_l_count;

/* 无人机、服务点和规划默认值 */
extern const drone_rule_t g_data_default_drone;
extern const app_point_t g_data_default_service_point;
extern const plan_rule_t g_data_default_plan;

#endif
