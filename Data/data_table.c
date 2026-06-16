#include "data_table.h"

/*
 * 演示用默认参数
 * 农田、配方、无人机和规划参数都集中在这里，算法只读结构体
 */
const crop_rule_t g_data_default_crop =
{
    1U,
    1U,
    180U,
    320U,
    450U,
    800U,
    20000UL,
    60000UL,
    60U,
    120U,
    2U,
    2U
};

const crop_rule_t g_data_crop_rule_table[] =
{
    {1U, 1U, 180U, 320U, 450U, 800U, 20000UL, 60000UL, 60U, 120U, 2U, 2U},
    {1U, 2U, 200U, 340U, 500U, 850U, 22000UL, 65000UL, 55U, 140U, 3U, 2U},
    {2U, 1U, 160U, 300U, 500U, 900U, 18000UL, 55000UL, 50U, 110U, 2U, 3U},
    {2U, 2U, 180U, 320U, 550U, 900U, 20000UL, 60000UL, 50U, 130U, 2U, 3U}
};

const uint16_t g_data_crop_rule_count = (uint16_t)(sizeof(g_data_crop_rule_table) / sizeof(g_data_crop_rule_table[0]));

const environment_input_t g_data_default_environment =
{
    280U,
    700U,
    35000UL,
    30U
};

const monitor_grid_t g_data_default_monitor_grid =
{
    /* 默认 7x7 农田，每块 20m x 20m */
    7U,
    7U,
    20000U,
    20000U
};

const monitor_auto_profile_t g_data_monitor_auto_profile =
{
    /* 健康/缺水比例用区间，病害类型按权重抽取 */
    40U,
    75U,
    10U,
    25U,
    {5U, 5U, 5U, 5U}
};

const pesticide_rule_t g_data_default_pesticide =
{
    1U,
    50U
};

const mix_recipe_t g_data_mix_recipe_table[] =
{
    /*
     * 单位面积配方，数值是 ml/m2 x100
     * 第一行是缺水喷水，后面四行是不同病害的农药任务
     */
    {SPRAY_CONTENT_WATER, DISEASE_TYPE_NONE, 0U, 0U, 2000U, 0U},
    {SPRAY_CONTENT_PESTICIDE, DISEASE_TYPE_BLIGHT, 1U, 100U, 1500U, 500U},
    {SPRAY_CONTENT_PESTICIDE, DISEASE_TYPE_RUST, 2U, 100U, 1600U, 400U},
    {SPRAY_CONTENT_PESTICIDE, DISEASE_TYPE_INSECT, 3U, 100U, 1400U, 600U},
    {SPRAY_CONTENT_PESTICIDE, DISEASE_TYPE_MILDEW, 4U, 100U, 1700U, 500U}
};

const uint16_t g_data_mix_recipe_count =
    (uint16_t)(sizeof(g_data_mix_recipe_table) / sizeof(g_data_mix_recipe_table[0]));

const uint16_t g_data_tank_capacity_l_table[] =
{
    /* UI 显示 L，业务层会转成 ml_x10 */
    5U,
    10U,
    15U,
    20U,
    25U,
    30U,
    40U,
    50U,
    60U
};

const uint16_t g_data_tank_capacity_l_count =
    (uint16_t)(sizeof(g_data_tank_capacity_l_table) / sizeof(g_data_tank_capacity_l_table[0]));

const drone_rule_t g_data_default_drone =
{
    /* 喷幅 5m，飞行速度 3m/s，药箱 40L */
    5000U,
    3000U,
    400000U
};

const app_point_t g_data_default_service_point =
{
    /* 固定服务点放在农田外侧 */
    -3000,
    -3000
};

const plan_rule_t g_data_default_plan =
{
    /* 服务点安全距离 1m，喷洒重叠率 10% */
    1000U,
    1000U
};
