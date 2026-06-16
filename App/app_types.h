#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <stdint.h>
#include "app_config.h"

/*
 * 公共业务类型
 *
 * 主要数据链：
 * monitor_output_t -> mix_output_t -> plan_output_t -> sim_output_t
 *
 * 单位约定：
 * - *_mm       毫米
 * - *_m2_x100  平方米 x100
 * - *_ml_x10   毫升 x10
 * - *_x100     百分比 x100
 */

typedef enum
{
    /* 当前流程只区分参数、容量和状态顺序三类错误 */
    APP_OK = 0,
    APP_ERR_PARAM,
    APP_ERR_LIMIT,
    APP_ERR_STATE
} app_result_t;

typedef enum
{
    /* app_core 的主流程状态 */
    APP_STATE_IDLE = 0,
    APP_STATE_MONITOR_DONE,
    APP_STATE_MIX_DONE,
    APP_STATE_PLAN_DONE,
    APP_STATE_SIM_RUNNING,
    APP_STATE_SIM_DONE
} app_state_t;

typedef enum
{
    /* 告警码不绑定 UI 文案 */
    APP_ALARM_NONE = 0,
    APP_ALARM_ROUTE_FINISHED,
    APP_ALARM_INVALID_DATA
} app_alarm_code_t;

typedef enum
{
    SIM_STATE_IDLE = 0,                 /* 仿真未运行 */
    SIM_STATE_SPRAYING,                 /* 正常作业中 */
    SIM_STATE_RETURNING_TO_HOME,        /* 正在返航服务点中 */
    SIM_STATE_REFILLING,                /* 正在服务点补给药液中 */
    SIM_STATE_RETURNING_TO_BREAKPOINT,   /* 正在重返断点中 */
    SIM_STATE_PAUSED                    /* 悬停暂停状态 */
} sim_state_t;

typedef struct
{
    /* 二维业务坐标，单位 mm */
    int32_t x_mm;
    int32_t y_mm;
} app_point_t;

typedef struct
{
    /* 农田边界当前按矩形生成，结构上保留多边形余量 */
    uint16_t vertex_count;
    app_point_t vertex[APP_MAX_FIELD_VERTEX_COUNT];
    uint32_t area_m2_x100;
} app_field_t;

typedef struct
{
    /* 目前只保留告警码 */
    app_alarm_code_t code;
} app_alarm_t;

typedef enum
{
    /* 初始化、自动检测、手动改单块三种模式 */
    MONITOR_MODE_INIT_ONLY = 0,
    MONITOR_MODE_AUTO_DETECT,
    MONITOR_MODE_MANUAL_SET
} monitor_mode_t;

typedef enum
{
    /* 健康不喷，缺水喷水，病害喷农药 */
    PLOT_STATE_HEALTHY = 0,
    PLOT_STATE_WATER_DEFICIT,
    PLOT_STATE_DISEASE
} plot_state_t;

typedef enum
{
    /* 非 NONE 值和配方表里的病害列对应 */
    DISEASE_TYPE_NONE = 0,
    DISEASE_TYPE_BLIGHT,
    DISEASE_TYPE_RUST,
    DISEASE_TYPE_INSECT,
    DISEASE_TYPE_MILDEW
} disease_type_t;

typedef enum
{
    /* 主批次按喷洒内容分组 */
    SPRAY_CONTENT_NONE = 0,
    SPRAY_CONTENT_WATER,
    SPRAY_CONTENT_PESTICIDE
} spray_content_t;

typedef struct
{
    /* 农田网格和单块尺寸 */
    uint16_t rows;
    uint16_t cols;
    uint16_t cell_width_mm;
    uint16_t cell_height_mm;
} monitor_grid_t;

typedef struct
{
    uint16_t temperature_x10;
    uint16_t humidity_x10;
    uint32_t light_lux;
    uint16_t wind_speed_x10;
} environment_input_t;

typedef struct
{
    environment_input_t input;
    uint16_t environment_factor_x100;
    uint8_t temperature_ok;
    uint8_t humidity_ok;
    uint8_t light_ok;
    uint8_t wind_ok;
    uint8_t suitable;
    uint8_t reserved[3];
} environment_status_t;

typedef struct
{
    /* mode 决定使用初始化、自动检测还是手动设置 */
    monitor_grid_t grid;
    environment_input_t environment;
    monitor_mode_t mode;
    uint32_t random_seed;
    uint8_t crop_type;
    uint8_t growth_stage;
    uint16_t manual_plot_id;
    plot_state_t manual_state;
    disease_type_t manual_disease_type;
} monitor_input_t;

typedef struct
{
    /* 单块地的检测结果，坐标取左上角 */
    uint16_t plot_id;
    uint16_t row;
    uint16_t col;
    int32_t x_mm;
    int32_t y_mm;
    uint32_t area_m2_x100;
    plot_state_t state;
    disease_type_t disease_type;
} plot_status_t;

typedef struct
{
    /* 按作业类型聚合后的区域；具体地块仍查 plot_status */
    spray_content_t spray_content;
    disease_type_t disease_type;
    uint16_t severity_x100;
} monitor_region_t;

typedef struct
{
    /* 监测统计，也用于生成 region */
    uint16_t healthy_plot_count;
    uint16_t water_deficit_plot_count;
    uint16_t disease_plot_count[APP_MAX_DISEASE_TYPE_COUNT];
    uint32_t total_spray_area_m2_x100;
} monitor_stats_t;

typedef struct
{
    /* 农田检测输出 */
    monitor_grid_t grid;
    app_field_t field;
    uint8_t crop_type;
    uint8_t growth_stage;
    uint16_t plot_count;
    plot_status_t plot_status[APP_MAX_PLOT_COUNT];
    uint16_t region_count;
    monitor_region_t region[APP_MAX_MONITOR_REGION_COUNT];
    environment_status_t environment_status;
    monitor_stats_t stats;
} monitor_output_t;

typedef struct
{
    uint32_t tank_capacity_ml_x10;
} mix_input_t;

typedef struct
{
    /* 单块地或切片；超出药箱容量时会被拆开 */
    uint16_t plot_id;
    int32_t min_x_mm;
    int32_t max_x_mm;
    int32_t min_y_mm;
    int32_t max_y_mm;
} mix_chunk_t;

typedef struct
{
    /* 一次装载任务 */
    uint16_t sub_batch_id;
    spray_content_t spray_content;
    disease_type_t disease_type;
    uint16_t severity_x100;
    uint16_t chunk_count;
    uint32_t liquid_ml_x10;
    int32_t min_x_mm;
    int32_t max_x_mm;
    int32_t min_y_mm;
    int32_t max_y_mm;
    int32_t center_x_mm;
    int32_t center_y_mm;
    mix_chunk_t chunk[APP_MAX_MIX_CHUNK_PER_SUB_BATCH];
} mix_sub_batch_t;

typedef struct
{
    /* 一组同类喷洒任务 */
    uint16_t main_batch_id;
    spray_content_t spray_content;
    disease_type_t disease_type;
    uint16_t severity_x100;
    uint16_t bucket_count;
    uint16_t sub_batch_start;
    uint16_t sub_batch_count;
} mix_main_batch_t;

typedef struct
{
    /* 配药汇总和非致命提示 */
    uint32_t total_liquid_ml_x10;
    uint32_t total_water_ml_x10;
    uint32_t total_pesticide_ml_x10;
    uint16_t total_bucket_count;
    uint16_t warning_count;
    char warning_text[APP_MIX_WARNING_TEXT_LEN];
} mix_plan_summary_t;

typedef struct
{
    /* main_batch 按类型分组，sub_batch 按药箱容量拆桶 */
    uint16_t main_batch_count;
    uint16_t sub_batch_count;
    mix_main_batch_t main_batch[APP_MAX_MIX_MAIN_BATCH_COUNT];
    mix_sub_batch_t sub_batch[APP_MAX_MIX_SUB_BATCH_COUNT];
    mix_plan_summary_t summary;
} mix_output_t;

typedef struct
{
    /*
     * 航点动作采用目标点语义：
     * action 为 PLAN_ACTION_SPRAY_ON 时，表示上一航点到当前航点这一段在喷洒
     */
    app_point_t point;
    uint8_t action;
    uint16_t sub_batch_id;
} waypoint_t;

typedef enum
{
    /*
     * 航点动作描述飞行段或操作点
     * 当前路径主要靠 TRANSIT 和 SPRAY_ON 区分是否喷洒
     */
    PLAN_ACTION_TAKEOFF = 0,
    PLAN_ACTION_TRANSIT,
    PLAN_ACTION_SPRAY_ON,
    PLAN_ACTION_REFILL,
    PLAN_ACTION_CHANGE_LIQUID,
    PLAN_ACTION_LAND
} plan_action_t;

typedef struct
{
    /* 路径规划后的一桶任务 */
    uint16_t sub_batch_id;
    uint16_t mix_sub_batch_index;
    uint16_t start_index;
    uint16_t waypoint_count;
    uint32_t path_distance_mm;
    uint32_t spray_distance_mm;
    uint32_t estimated_time_ms;
} sub_batch_t;

typedef struct
{
    /* 全局路径统计 */
    app_point_t service_point;
    uint32_t total_distance_mm;
    uint32_t total_spray_distance_mm;
    uint32_t total_estimated_time_ms;
} plan_summary_t;

typedef struct
{
    /* 全部桶任务拼接后的航点路径 */
    uint16_t sub_batch_count;
    uint16_t waypoint_count;
    sub_batch_t sub_batch[APP_MAX_SUB_BATCH_COUNT];
    waypoint_t waypoint[APP_MAX_WAYPOINT_COUNT];
    plan_summary_t summary;
} plan_output_t;

typedef struct
{
    /* 32-bit 对齐成员 */
    app_point_t current_position;
    app_point_t breakpoint_position;
    uint32_t elapsed_time_ms;
    uint32_t route_distance_done_mm;
    uint32_t spray_distance_done_mm;
    uint32_t remain_liquid_ml_x10;
    uint32_t refill_timer_ms;
    sim_state_t state;              /* 状态机状态 */

    /* 16-bit 对齐成员 */
    uint16_t current_waypoint_index;
    uint16_t progress_x100;
    uint16_t cover_rate_x100;
    uint16_t battery_x100;
    uint16_t alarm_count;
    uint16_t refill_count;
    uint16_t charge_count;

    /* 8-bit 对齐成员 */
    uint8_t running;
    uint8_t rth_trigger_source;     /* 0-none, 1-low liquid, 2-manual, 3-low battery */
    uint8_t pause_resume_state;     /* 暂停前的 sim_state_t，用于再次启动时续飞 */
    uint8_t reserved[3];            /* 显式填充对齐 */

    /* 数组 (4字节对齐) */
    app_alarm_t alarm[APP_MAX_ALARM_COUNT];
} sim_output_t;

typedef struct
{
    /* app_core 持有的运行态 */
    app_state_t state;
    monitor_input_t monitor_input;
    mix_input_t mix_input;
    monitor_output_t monitor_output;
    mix_output_t mix_output;
    plan_output_t plan_output;
    sim_output_t sim_output;
} app_runtime_t;

#endif
