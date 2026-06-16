#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>

/* 业务数组的静态容量上限固定分配更容易控制 MCU 端 RAM */
#define APP_MAX_SUB_BATCH_COUNT         256U
/* 航点分两级限制：单桶和全局 */
#define APP_MAX_WAYPOINT_PER_SUB_BATCH  256U
#define APP_MAX_WAYPOINT_COUNT          1024U
/* 当前界面和路径数组按最大 32x32 地块设计 */
#define APP_MAX_FIELD_VERTEX_COUNT      16U
#define APP_MAX_ALARM_COUNT             16U
#define APP_MAX_FIELD_ROWS              32U
#define APP_MAX_FIELD_COLS              32U
#define APP_MAX_PLOT_COUNT              (APP_MAX_FIELD_ROWS * APP_MAX_FIELD_COLS)
#define APP_MAX_DISEASE_TYPE_COUNT      4U
#define APP_MAX_MONITOR_REGION_COUNT    (APP_MAX_DISEASE_TYPE_COUNT + 1U)
/* chunk 是单桶里的地块切片，上限用于卡住异常拆分 */
#define APP_MAX_MIX_MAIN_BATCH_COUNT    16U
#define APP_MAX_MIX_SUB_BATCH_PER_MAIN  128U
#define APP_MAX_MIX_SUB_BATCH_COUNT     256U
#define APP_MAX_MIX_CHUNK_PER_SUB_BATCH 16U
#define APP_MIX_WARNING_TEXT_LEN        256U

/* 10ms 是业务节拍；倍速只影响仿真播放 */
#define APP_TICK_PERIOD_MS              10U
#define APP_SIM_TIME_SCALE_X            20U

/* 变量配药严重度倍率 */
#define APP_SEVERITY_MIN_X100           60U
#define APP_SEVERITY_LIGHT_X100         80U
#define APP_SEVERITY_MEDIUM_X100        100U
#define APP_SEVERITY_HEAVY_X100         120U
#define APP_SEVERITY_MAX_X100           140U
#define APP_ENV_FACTOR_MAX_X100         120U

#endif
