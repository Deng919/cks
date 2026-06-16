#ifndef UI_MODULE_INTERNAL_H
#define UI_MODULE_INTERNAL_H

#include <stdint.h>
#include <stddef.h>

#include "app_core.h"
#include "app_config.h"
#include "app_types.h"
#include "data_table.h"
#include "lvgl.h"
#include "mix_module.h"
#include "monitor_module.h"
#include "plan_module.h"
#include "sim_module.h"

/* UI 模块内部声明，业务数据仍由 app_core 统一持有 */

/* UI 使用的中文字体资源 */
LV_FONT_DECLARE(lv_font_chinese_18);
LV_FONT_DECLARE(lv_font_chinese_20);
LV_FONT_DECLARE(lv_font_chinese_24);

typedef enum
{
    /* 监测页的两种主动动作 */
    UI_MONITOR_ACTION_INIT = 0,
    UI_MONITOR_ACTION_AUTO
} ui_monitor_action_t;

typedef enum
{
    /* 顶部导航的四个页面 */
    UI_PAGE_MONITOR = 0,
    UI_PAGE_MIX,
    UI_PAGE_PLAN,
    UI_PAGE_SIM
} ui_page_t;

typedef struct
{
    /*
     * UI 上下文只保存控件指针和页面状态
     * 监测、配药、规划、仿真结果统一从 app_core 读取
     */
    /* 根屏幕和顶部导航 */
    lv_obj_t *screen;
    lv_obj_t *title_label;
    lv_obj_t *state_label;
    lv_obj_t *nav_monitor_btn;
    lv_obj_t *nav_mix_btn;
    lv_obj_t *nav_plan_btn;
    lv_obj_t *nav_sim_btn;

    /* 四个主页面 */
    lv_obj_t *monitor_page;
    lv_obj_t *mix_page;
    lv_obj_t *plan_page;
    lv_obj_t *sim_page;

    /* 监测页控件 */
    lv_obj_t *auto_panel;
    lv_obj_t *manual_panel;
    lv_obj_t *info_panel;
    lv_obj_t *summary_label;
    lv_obj_t *detail_label;
    lv_obj_t *grid_panel;
    lv_obj_t *monitor_stats_popup;

    /* 配药页控件 */
    lv_obj_t *mix_state_label;
    lv_obj_t *mix_warning_label;
    lv_obj_t *mix_metric_panel;
    lv_obj_t *mix_main_visual_panel;
    lv_obj_t *mix_bucket_visual_panel;
    lv_obj_t *mix_capacity_dd;
    lv_obj_t *button_mix_run;
    lv_obj_t *button_plan_run;
    lv_obj_t *button_sim_start;
    lv_obj_t *button_sim_stop;
    lv_obj_t *button_sim_speed;
    lv_obj_t *button_sim_recall;
    lv_obj_t *button_sim_resume;
    lv_obj_t *sim_speed_label;

    /* 监测输入控件 */
    lv_obj_t *button_init;
    lv_obj_t *button_auto;
    lv_obj_t *button_stats;
    lv_obj_t *button_manual_apply;
    lv_obj_t *crop_type_dd;
    lv_obj_t *growth_stage_dd;
    lv_obj_t *manual_plot_id_ta;
    lv_obj_t *manual_state_dd;
    lv_obj_t *manual_disease_dd;
    lv_obj_t *manual_keyboard;

    /* 规划页控件 */
    lv_obj_t *plan_header_panel;
    lv_obj_t *plan_state_label;
    lv_obj_t *plan_detail_title_label;
    lv_obj_t *plan_metric_panel;
    lv_obj_t *plan_timeline_panel;
    lv_obj_t *plan_detail_panel;

    /* 仿真页控件 */
    lv_obj_t *sim_state_label;
    lv_obj_t *sim_progress_label;
    lv_obj_t *sim_progress_bar;
    lv_obj_t *sim_battery_label;
    lv_obj_t *sim_battery_bar;
    lv_obj_t *sim_summary_label;
    lv_obj_t *sim_grid_panel;
    lv_obj_t *sim_grid_plane_icon;
    lv_obj_t *sim_grid_cell[APP_MAX_PLOT_COUNT];
    lv_obj_t *sim_current_label;
    lv_obj_t *sim_alarm_label;

    /* 动态对象数组和缓存 */
    lv_obj_t *grid_cell[APP_MAX_PLOT_COUNT];
    lv_obj_t *manual_keypad[12];
    char mix_capacity_options[128];

    /* 当前页面和选择状态 */
    uint16_t grid_cell_count;
    uint16_t selected_plot_index;
    uint16_t selected_plan_sub_batch_index;
    ui_page_t current_page;
    monitor_input_t monitor_input;
} ui_module_ctx_t;

typedef struct
{
    /*
     * 仿真地图缩放布局
     * scale_span_mm/scale_px 使用同一比例，避免农田被拉伸
     */
    int32_t min_x_mm;
    int32_t max_x_mm;
    int32_t min_y_mm;
    int32_t max_y_mm;
    lv_coord_t origin_x;
    lv_coord_t origin_y;
    lv_coord_t width;
    lv_coord_t height;
    lv_coord_t scale_px;
    uint32_t scale_span_mm;
} ui_sim_grid_layout_t;

/* 全局 UI 上下文 */
extern ui_module_ctx_t g_ui_ctx;

/* 设置页面主标题字体 */
void ui_apply_title_font(lv_obj_t *label);
/* 设置分区标题字体 */
void ui_apply_section_font(lv_obj_t *label);
/* 查找容量下拉框对应的选项 */
uint16_t ui_mix_find_capacity_index(uint32_t tank_capacity_ml_x10);
/* 生成容量下拉框选项文本 */
void ui_mix_build_capacity_options(void);
/* 读取当前容量选项对应的桶容量 */
uint32_t ui_mix_selected_capacity_ml_x10(void);
/* 累加格式化写入长度 */
size_t ui_text_advance(size_t current_len, size_t buffer_size, int write_len);
/* 返回地块状态显示名 */
const char *ui_plot_state_name(plot_state_t state);
/* 返回病害类型显示名 */
const char *ui_disease_name(disease_type_t disease_type);
/* 返回规划动作显示名 */
const char *ui_plan_action_name(uint8_t action);
/* 返回告警码显示名 */
const char *ui_alarm_name(app_alarm_code_t code);
/* 返回应用状态显示名 */
const char *ui_app_state_name(app_state_t state);
/* 返回毫米转米后的整数部分 */
long ui_mm_to_m_integer(int32_t value_mm);
/* 返回毫米转米后的一位小数 */
long ui_mm_to_m_decimal(int32_t value_mm);
/* 根据地块状态选择 UI 颜色 */
lv_color_t ui_plot_color(const plot_status_t *plot);
/* 根据喷洒内容和病害选择 UI 颜色 */
lv_color_t ui_content_color(spray_content_t spray_content, disease_type_t disease_type);
/* 清空容器里的动态控件 */
void ui_clean_container(lv_obj_t *obj);
/* 在父容器内创建文本标签 */
void ui_add_label(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y, lv_coord_t w, uint8_t section_font, lv_color_t color);
/* 创建颜色图例项 */
void ui_create_legend_item(lv_obj_t *parent, const char *text, lv_color_t color, lv_coord_t x, lv_coord_t y);

/* 准备默认监测输入 */
void ui_monitor_set_default_input(void);
/* 刷新监测页显示 */
void ui_monitor_update_view(app_result_t result);
/* 执行监测页动作 */
void ui_monitor_run(ui_monitor_action_t action);
/* 释放监测网格控件 */
void ui_monitor_release_grid_cells(void);
/* 回到监测页时重建网格 */
void ui_monitor_reload_grid(void);

/* 刷新配药页显示 */
void ui_mix_update_view(app_result_t result);
/* 释放配药页动态内容 */
void ui_mix_release_visual_content(void);
/* 回到配药页时重建内容 */
void ui_mix_reload_view(void);
/* 刷新规划页显示 */
void ui_plan_update_view(app_result_t result);
/* 释放规划页较重的动态文本和地图 */
void ui_plan_release_heavy_text(void);
/* 回到规划页时重建内容 */
void ui_plan_reload_view(void);

/* 计算仿真地图布局 */
void ui_sim_calc_grid_layout(const monitor_output_t *monitor_output, const plan_output_t *plan_output, ui_sim_grid_layout_t *layout);
/* 把业务 x 坐标映射到仿真地图 */
lv_coord_t ui_sim_map_x(const ui_sim_grid_layout_t *layout, int32_t x_mm);
/* 把业务 y 坐标映射到仿真地图 */
lv_coord_t ui_sim_map_y(const ui_sim_grid_layout_t *layout, int32_t y_mm);
/* 刷新仿真地图 */
void ui_sim_update_map(void);
/* 刷新仿真进度条 */
void ui_sim_update_progress_bar(const sim_output_t *sim_output);
/* 刷新仿真页显示 */
void ui_sim_update_view(app_result_t result);
/* 按需创建仿真页内容 */
void ui_sim_ensure_page_content(void);
/* 释放仿真页动态内容 */
void ui_sim_release_page_content(void);

/* 处理监测网格点击 */
void ui_grid_cell_event_cb(lv_event_t *e);
/* 处理顶部导航点击 */
void ui_nav_event_cb(lv_event_t *e);
/* 处理业务按钮点击 */
void ui_button_event_cb(lv_event_t *e);

#endif
