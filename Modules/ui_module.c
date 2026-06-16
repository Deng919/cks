#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "app_math.h"
#include "ui_module.h"
#include "ui_module_internal.h"

ui_module_ctx_t g_ui_ctx;

static uint16_t g_ui_sim_total_chunks[APP_MAX_PLOT_COUNT];

static uint16_t g_ui_sim_done_chunks[APP_MAX_PLOT_COUNT];

/* 追加格式化写入长度，避免缓冲区越界后继续累加 */
size_t ui_text_advance(size_t current_len, size_t buffer_size, int write_len);

/* 处理手动地块编号输入框事件 */
static void ui_manual_plot_id_event_cb(lv_event_t *e);

/* 保留手动键盘容器事件入口 */
static void ui_manual_keyboard_event_cb(lv_event_t *e);

/* 处理手动键盘按钮事件 */
static void ui_manual_keypad_btn_event_cb(lv_event_t *e);
static void ui_monitor_show_stats_popup(void);
static void ui_monitor_stats_close_event_cb(lv_event_t *e);
static const char *ui_cn_number_name(uint16_t value);

#define UI_SIM_TEXT_REFRESH_MS 1000U

static void ui_monitor_apply_crop_selection(void)
{
    uint16_t crop_index;
    uint16_t stage_index;
    crop_index = 0U;
    stage_index = 0U;
    if (g_ui_ctx.crop_type_dd != 0) {
        crop_index = lv_dropdown_get_selected(g_ui_ctx.crop_type_dd);
    }
    if (g_ui_ctx.growth_stage_dd != 0) {
        stage_index = lv_dropdown_get_selected(g_ui_ctx.growth_stage_dd);
    }
    g_ui_ctx.monitor_input.crop_type = (uint8_t)(crop_index + 1U);
    g_ui_ctx.monitor_input.growth_stage = (uint8_t)(stage_index + 1U);
}

static const char *ui_crop_name(uint8_t crop_type)
{
    switch (crop_type) {
        case 1U:
            return "\346\260\264\347\250\273"; /* 水稻 */
        case 2U:
            return "\345\260\217\351\272\246"; /* 小麦 */
        default:
            return "\346\234\252\347\237\245\344\275\234\347\211\251"; /* 未知作物 */
    }
}

static const char *ui_growth_stage_name(uint8_t growth_stage)
{
    switch (growth_stage) {
        case 1U:
            return "\350\213\227\346\234\237"; /* 苗期 */
        case 2U:
            return "\346\210\220\351\225\277\346\234\237"; /* 成长期 */
        default:
            return "\346\234\252\347\237\245\351\230\266\346\256\265"; /* 未知阶段 */
    }
}

/* 判断是否到达下一次低频刷新时间 */
static uint8_t ui_elapsed_reached(uint32_t now_ms, uint32_t last_ms, uint32_t interval_ms)
{
    /* 仿真文字不用跟主循环一样高频刷新 */
    if (last_ms == 0xFFFFFFFFUL) {
        return 1U;
    }
    if (now_ms < last_ms) {
        return 1U;
    }
    return ((now_ms - last_ms) >= interval_ms) ? 1U : 0U;
}

/* 设置页面主标题字体 */
void ui_apply_title_font(lv_obj_t *label)
{
    if (label != 0) {
        lv_obj_set_style_text_font(label, &lv_font_chinese_24, 0);
    }
}

/* 设置分区标题字体 */
void ui_apply_section_font(lv_obj_t *label)
{
    if (label != 0) {
        lv_obj_set_style_text_font(label, &lv_font_chinese_20, 0);
    }
}

/* 根据业务容量查找下拉框索引 */
uint16_t ui_mix_find_capacity_index(uint32_t tank_capacity_ml_x10)
{
    uint16_t i;
    for (i = 0U; i < g_data_tank_capacity_l_count; i++) {
        if (((uint32_t)g_data_tank_capacity_l_table[i] * 10000U) == tank_capacity_ml_x10) {
            return i;
        }
    }
    return 0U;
}

/* 生成药箱容量下拉框选项 */
void ui_mix_build_capacity_options(void)
{
    size_t len;
    uint16_t i;
    /* LVGL dropdown 选项用换行分隔 */
    len = 0U;
    g_ui_ctx.mix_capacity_options[0] = '\0';
    for (i = 0U; i < g_data_tank_capacity_l_count; i++) {
        int write_len;
        write_len = snprintf(g_ui_ctx.mix_capacity_options + len,
                             sizeof(g_ui_ctx.mix_capacity_options) - len,
                             (i == 0U) ? "%uL" : "\n%uL",
                             g_data_tank_capacity_l_table[i]);
        len = ui_text_advance(len, sizeof(g_ui_ctx.mix_capacity_options), write_len);
        if (len >= (sizeof(g_ui_ctx.mix_capacity_options) - 1U)) {
            break;
        }
    }
}

/* 读取当前选中的药箱容量，返回 ml_x10 */
uint32_t ui_mix_selected_capacity_ml_x10(void)
{
    uint16_t selected;
    selected = lv_dropdown_get_selected(g_ui_ctx.mix_capacity_dd);
    if (selected >= g_data_tank_capacity_l_count) {
        selected = 0U;
    }
    return (uint32_t)g_data_tank_capacity_l_table[selected] * 10000U;
}

/* 根据 snprintf 结果安全推进字符串写入位置 */
size_t ui_text_advance(size_t current_len, size_t buffer_size, int write_len)
{
    size_t remaining;
    /* snprintf 返回值可能大于剩余空间 */
    if ((buffer_size == 0U) || (current_len >= buffer_size)) {
        return 0U;
    }
    if (write_len < 0) {
        return current_len;
    }
    remaining = buffer_size - current_len;
    if ((size_t)write_len >= remaining) {
        return buffer_size - 1U;
    }
    return current_len + (size_t)write_len;
}

/* 地块状态中文名 */
const char *ui_plot_state_name(plot_state_t state)
{
    switch (state) {
    case PLOT_STATE_HEALTHY:
        return "\345\201\245\345\272\267"; /* 健康 */
    case PLOT_STATE_WATER_DEFICIT:
        return "\347\274\272\346\260\264"; /* 缺水 */
    case PLOT_STATE_DISEASE:
        return "\347\227\205\345\256\263"; /* 病害 */
    default:
        return "\346\234\252\347\237\245"; /* 未知 */
    }
}

/* 病害类型中文名 */
const char *ui_disease_name(disease_type_t disease_type)
{
    switch (disease_type) {
    case DISEASE_TYPE_BLIGHT:
        return "\346\236\257\350\220\216\347\227\205"; /* 枯萎病 */
    case DISEASE_TYPE_RUST:
        return "\351\224\210\347\227\205"; /* 锈病 */
    case DISEASE_TYPE_INSECT:
        return "\350\231\253\345\256\263"; /* 虫害 */
    case DISEASE_TYPE_MILDEW:
        return "\351\234\211\347\227\205"; /* 霉病 */
    default:
        return "\346\227\240"; /* 无 */
    }
}

/* 航点动作中文名 */
const char *ui_plan_action_name(uint8_t action)
{
    /* action 采用终点语义，SPRAY_ON 表示上一段在喷洒 */
    switch ((plan_action_t)action) {
    case PLAN_ACTION_TAKEOFF:
        return "\350\265\267\351\243\236"; /* 起飞 */
    case PLAN_ACTION_TRANSIT:
        return "\350\275\254\345\234\272"; /* 转场 */
    case PLAN_ACTION_SPRAY_ON:
        return "\345\226\267\346\264\222"; /* 喷洒 */
    case PLAN_ACTION_REFILL:
        return "\347\273\255\346\266\262"; /* 续液 */
    case PLAN_ACTION_CHANGE_LIQUID:
        return "\346\215\242\346\266\262"; /* 换液 */
    case PLAN_ACTION_LAND:
        return "\351\231\215\350\220\275"; /* 降落 */
    default:
        return "\346\227\240"; /* 无 */
    }
}

/* 仿真告警中文名 */
const char *ui_alarm_name(app_alarm_code_t code)
{
    switch (code) {
    case APP_ALARM_NONE:
        return "\346\227\240"; /* 无 */
    case APP_ALARM_ROUTE_FINISHED:
        return "\350\210\252\347\272\277\345\256\214\346\210\220"; /* 航线完成 */
    case APP_ALARM_INVALID_DATA:
        return "\346\225\260\346\215\256\346\227\240\346\225\210"; /* 数据无效 */
    default:
        return "\346\234\252\347\237\245\346\212\245\350\255\246"; /* 未知报警 */
    }
}

/* app_core 状态中文名 */
const char *ui_app_state_name(app_state_t state)
{
    switch (state) {
    case APP_STATE_IDLE:
        return "\347\251\272\351\227\262"; /* 空闲 */
    case APP_STATE_MONITOR_DONE:
        return "\347\233\221\346\265\213\345\256\214\346\210\220"; /* 监测完成 */
    case APP_STATE_MIX_DONE:
        return "\351\205\215\350\215\257\345\256\214\346\210\220"; /* 配药完成 */
    case APP_STATE_PLAN_DONE:
        return "\350\247\204\345\210\222\345\256\214\346\210\220"; /* 规划完成 */
    case APP_STATE_SIM_RUNNING:
        return "\344\273\277\347\234\237\350\277\220\350\241\214\344\270\255"; /* 仿真运行中 */
    case APP_STATE_SIM_DONE:
        return "\344\273\277\347\234\237\345\256\214\346\210\220"; /* 仿真完成 */
    default:
        return "\346\234\252\347\237\245"; /* 未知 */
    }
}

/* 毫米转米后的一位小数整数部分 */
long ui_mm_to_m_integer(int32_t value_mm)
{
    return (long)(value_mm / 1000);
}

/* 毫米转米后的一位小数小数部分 */
long ui_mm_to_m_decimal(int32_t value_mm)
{
    int32_t remain_mm;
    remain_mm = value_mm % 1000;
    if (remain_mm < 0) {
        remain_mm = -remain_mm;
    }
    return (long)(remain_mm / 100);
}

/* 根据地块状态选择颜色 */
lv_color_t ui_plot_color(const plot_status_t *plot)
{
    if (plot->state == PLOT_STATE_HEALTHY) {
        return lv_palette_main(LV_PALETTE_GREEN);
    }
    if (plot->state == PLOT_STATE_WATER_DEFICIT) {
        return lv_palette_main(LV_PALETTE_BLUE);
    }
    switch (plot->disease_type) {
    case DISEASE_TYPE_BLIGHT:
        return lv_palette_main(LV_PALETTE_RED);
    case DISEASE_TYPE_RUST:
        return lv_palette_main(LV_PALETTE_ORANGE);
    case DISEASE_TYPE_INSECT:
        return lv_palette_main(LV_PALETTE_AMBER);
    case DISEASE_TYPE_MILDEW:
        return lv_palette_main(LV_PALETTE_PURPLE);
    default:
        return lv_palette_main(LV_PALETTE_GREY);
    }
}

/* 根据喷洒内容 and 病害类型选择主题色 */
lv_color_t ui_content_color(spray_content_t spray_content, disease_type_t disease_type)
{
    if (spray_content == SPRAY_CONTENT_WATER) {
        return lv_palette_main(LV_PALETTE_BLUE);
    }
    switch (disease_type) {
    case DISEASE_TYPE_BLIGHT:
        return lv_palette_main(LV_PALETTE_RED);
    case DISEASE_TYPE_RUST:
        return lv_palette_main(LV_PALETTE_ORANGE);
    case DISEASE_TYPE_INSECT:
        return lv_palette_main(LV_PALETTE_AMBER);
    case DISEASE_TYPE_MILDEW:
        return lv_palette_main(LV_PALETTE_PURPLE);
    default:
        return lv_palette_main(LV_PALETTE_GREY);
    }
}

/* 清空容器内的动态子对象 */
void ui_clean_container(lv_obj_t *obj)
{
    if (obj != 0) {
        lv_obj_clean(obj);
    }
}

/* 创建固定宽度、可换行的文本标签 */
void ui_add_label(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y, lv_coord_t w, uint8_t section_font, lv_color_t color)
{
    lv_obj_t *label;
    /* 创建正文标签，用于显示页面上的普通文本 */
    label = lv_label_create(parent);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, w);
    lv_obj_set_pos(label, x, y);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    if (section_font != 0U) {
        ui_apply_section_font(label);
    }
}

/* 创建一个色块加文字的图例项 */
void ui_create_legend_item(lv_obj_t *parent,const char *text,lv_color_t color,lv_coord_t x,lv_coord_t y)
{
    lv_obj_t *color_box;
    lv_obj_t *label;
    /* 创建图例色块，颜色对应地块状态或路径类型 */
    color_box = lv_obj_create(parent);
    lv_obj_set_size(color_box, 18, 18);
    lv_obj_set_pos(color_box, x, y);
    lv_obj_clear_flag(color_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(color_box, 3, LV_PART_MAIN);
    lv_obj_set_style_border_width(color_box, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(color_box, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
    lv_obj_set_style_bg_color(color_box, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(color_box, LV_OPA_COVER, LV_PART_MAIN);
    /* 创建图例文字标签，说明色块含义 */
    label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, (lv_coord_t)(x + 26), (lv_coord_t)(y - 1));
}

/* 切换当前页面，并按需释放离开页面的重对象 */
static void ui_switch_page(ui_page_t page)
{
    ui_page_t previous_page;
    /* release heavy page objects before switching */
    previous_page = g_ui_ctx.current_page;
    if ((g_ui_ctx.monitor_stats_popup != 0) && (page != UI_PAGE_MONITOR)) {
        lv_obj_del(g_ui_ctx.monitor_stats_popup);
        g_ui_ctx.monitor_stats_popup = 0;
    }
    if ((previous_page == UI_PAGE_MONITOR) && (page != UI_PAGE_MONITOR)) {
        ui_monitor_release_grid_cells();
    }
    if ((previous_page == UI_PAGE_SIM) && (page != UI_PAGE_SIM)) {
        ui_sim_release_page_content();
    }
    if ((previous_page == UI_PAGE_MIX) && (page != UI_PAGE_MIX)) {
        ui_mix_release_visual_content();
    }
    if ((previous_page == UI_PAGE_PLAN) && (page != UI_PAGE_PLAN)) {
        ui_plan_release_heavy_text();
    }
    g_ui_ctx.current_page = page;
    if (page == UI_PAGE_MONITOR) {
        lv_obj_clear_flag(g_ui_ctx.monitor_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui_ctx.mix_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui_ctx.plan_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui_ctx.sim_page, LV_OBJ_FLAG_HIDDEN);
        ui_monitor_reload_grid();
    } else if (page == UI_PAGE_MIX) {
        lv_obj_add_flag(g_ui_ctx.monitor_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_ui_ctx.mix_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui_ctx.plan_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui_ctx.sim_page, LV_OBJ_FLAG_HIDDEN);
        ui_mix_reload_view();
    } else if (page == UI_PAGE_PLAN) {
        lv_obj_add_flag(g_ui_ctx.monitor_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui_ctx.mix_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_ui_ctx.plan_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui_ctx.sim_page, LV_OBJ_FLAG_HIDDEN);
        ui_plan_reload_view();
    } else {
        lv_obj_add_flag(g_ui_ctx.monitor_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui_ctx.mix_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_ui_ctx.plan_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_ui_ctx.sim_page, LV_OBJ_FLAG_HIDDEN);
    }
}

/* 判断当前是否具备正常显示仿真页的规划数据 */
static app_result_t ui_sim_current_view_result(void)
{
    const plan_output_t *plan_output;
    app_state_t state;
    plan_output = app_core_get_plan_output();
    state = app_core_get_state();
    if ((plan_output == 0) ||
        (plan_output->waypoint_count == 0U) ||
        (plan_output->summary.total_distance_mm == 0U)) {
        return APP_ERR_STATE;
    }
    if ((state == APP_STATE_PLAN_DONE) || (state == APP_STATE_SIM_RUNNING) || (state == APP_STATE_SIM_DONE)) {
        return APP_OK;
    }
    return APP_ERR_STATE;
}

/* 读取手动地块编号输入框 */
static uint16_t ui_read_manual_plot_id(void)
{
    const char *text;
    uint16_t value;
    if (g_ui_ctx.manual_plot_id_ta == 0) {
        return 0U;
    }
    text = lv_textarea_get_text(g_ui_ctx.manual_plot_id_ta);
    value = 0U;
    if (app_parse_u16_bounded(text, 1U, APP_MAX_PLOT_COUNT, &value) == 0U) {
        return 0U;
    }
    return value;
}

/* 应用手动地块状态修改 */
static void ui_run_manual_apply(void)
{
    app_result_t result;
    const monitor_output_t *current_output;
    uint16_t state_index;
    uint16_t disease_index;
    /* 基于当前网格组织一次手动监测输入 */
    current_output = app_core_get_monitor_output();
    ui_monitor_set_default_input();
    if (current_output->plot_count > 0U) {
        g_ui_ctx.monitor_input.grid = current_output->grid;
    }
    g_ui_ctx.monitor_input.mode = MONITOR_MODE_MANUAL_SET;
    g_ui_ctx.monitor_input.manual_plot_id = ui_read_manual_plot_id();
    /* 把下拉框选项转换成业务枚举 */
    state_index = lv_dropdown_get_selected(g_ui_ctx.manual_state_dd);
    if (state_index == 0U) {
        g_ui_ctx.monitor_input.manual_state = PLOT_STATE_HEALTHY;
        g_ui_ctx.monitor_input.manual_disease_type = DISEASE_TYPE_NONE;
    } else if (state_index == 1U) {
        g_ui_ctx.monitor_input.manual_state = PLOT_STATE_WATER_DEFICIT;
        g_ui_ctx.monitor_input.manual_disease_type = DISEASE_TYPE_NONE;
    } else {
        g_ui_ctx.monitor_input.manual_state = PLOT_STATE_DISEASE;
        disease_index = lv_dropdown_get_selected(g_ui_ctx.manual_disease_dd);
        switch (disease_index) {
        case 1U:
            g_ui_ctx.monitor_input.manual_disease_type = DISEASE_TYPE_BLIGHT;
            break;
        case 2U:
            g_ui_ctx.monitor_input.manual_disease_type = DISEASE_TYPE_RUST;
            break;
        case 3U:
            g_ui_ctx.monitor_input.manual_disease_type = DISEASE_TYPE_INSECT;
            break;
        case 4U:
            g_ui_ctx.monitor_input.manual_disease_type = DISEASE_TYPE_MILDEW;
            break;
        default:
            g_ui_ctx.monitor_input.manual_disease_type = DISEASE_TYPE_BLIGHT;
            break;
        }
    }
    /* 首次没有监测结果时先运行一次监测 */
    if (current_output->plot_count == 0U) {
        result = app_core_run_monitor(&g_ui_ctx.monitor_input);
    } else {
        result = app_core_apply_manual_monitor(&g_ui_ctx.monitor_input);
    }
    ui_monitor_update_view(result);
    if (result == APP_OK) {
        /* 手动修改会让后续配药、规划、仿真结果失效 */
        ui_mix_update_view(APP_ERR_STATE);
        ui_plan_update_view(APP_ERR_STATE);
        ui_sim_update_view(APP_ERR_STATE);
    }
}

/* 处理所有业务按钮事件 */
static const monitor_region_t *ui_find_monitor_region(const monitor_output_t *output,
                                                       spray_content_t spray_content,
                                                       disease_type_t disease_type)
{
    uint16_t i;
    if (output == 0) {
        return 0;
    }
    for (i = 0U; i < output->region_count; i++) {
        if ((output->region[i].spray_content == spray_content) &&
            (output->region[i].disease_type == disease_type)) {
            return &output->region[i];
        }
    }
    return 0;
}

static const char *ui_severity_name(uint16_t severity_x100)
{
    if (severity_x100 >= 120U) {
        return "\351\207\215\345\272\246"; /* 重度 */
    }
    if (severity_x100 >= 100U) {
        return "\344\270\255\345\272\246"; /* 中度 */
    }
    return "\350\275\273\345\272\246"; /* 轻度 */
}

static void ui_monitor_stats_close_event_cb(lv_event_t *e)
{
    lv_obj_t *popup;
    popup = (lv_obj_t *)lv_event_get_user_data(e);
    if (popup != 0) {
        if (popup == g_ui_ctx.monitor_stats_popup) {
            g_ui_ctx.monitor_stats_popup = 0;
        }
        lv_obj_del(popup);
    }
}

static void ui_monitor_show_stats_popup(void)
{
    const monitor_output_t *output;
    lv_obj_t *popup;
    lv_obj_t *title;
    lv_obj_t *close_btn;
    lv_obj_t *label;
    lv_obj_t *bar;
    uint16_t counts[6];
    const char *names[6];
    lv_color_t colors[6];
    uint16_t total;
    uint16_t i;
    uint16_t disease_total;
    uint16_t priority_rank;
    uint8_t used_disease[APP_MAX_DISEASE_TYPE_COUNT];
    lv_coord_t popup_w;
    lv_coord_t popup_h;
    lv_coord_t parent_w;
    lv_coord_t parent_h;
    char text[160];
    output = app_core_get_monitor_output();
    if (g_ui_ctx.monitor_stats_popup != 0) {
        lv_obj_del(g_ui_ctx.monitor_stats_popup);
        g_ui_ctx.monitor_stats_popup = 0;
    }
    parent_w = lv_obj_get_width(g_ui_ctx.monitor_page);
    parent_h = lv_obj_get_height(g_ui_ctx.monitor_page);
    popup_w = 620;
    popup_h = 470;
    if (popup_w > parent_w) {
        popup_w = (lv_coord_t)(parent_w - 24);
    }
    if (popup_h > parent_h) {
        popup_h = (lv_coord_t)(parent_h - 24);
    }
    popup = lv_obj_create(g_ui_ctx.monitor_page);
    g_ui_ctx.monitor_stats_popup = popup;
    lv_obj_set_size(popup, popup_w, popup_h);
    lv_obj_set_pos(popup, (lv_coord_t)((parent_w - popup_w) / 2), (lv_coord_t)((parent_h - popup_h) / 2));
    lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(popup, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(popup, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(popup, 2, 0);
    lv_obj_set_style_radius(popup, 8, 0);
    lv_obj_move_foreground(popup);
    title = lv_label_create(popup);
    lv_label_set_text(title, "\346\235\241\345\275\242\347\273\237\350\256\241\345\233\276"); /* 条形统计图 */
    ui_apply_section_font(title);
    lv_obj_set_pos(title, 16, 10);
    close_btn = lv_btn_create(popup);
    lv_obj_set_size(close_btn, 70, 32);
    lv_obj_set_pos(close_btn, (lv_coord_t)(popup_w - 106), 10);
    lv_obj_add_event_cb(close_btn, ui_monitor_stats_close_event_cb, LV_EVENT_CLICKED, popup);
    label = lv_label_create(close_btn);
    lv_label_set_text(label, "\345\205\263\351\227\255"); /* 关闭 */
    lv_obj_center(label);
    if ((output == 0) || (output->plot_count == 0U)) {
        label = lv_label_create(popup);
        lv_label_set_text(label, "\346\232\202\346\227\240\347\233\221\346\265\213\346\225\260\346\215\256"); /* 暂无监测数据 */
        lv_obj_set_pos(label, (lv_coord_t)((popup_w - 150) / 2), (lv_coord_t)((popup_h - 24) / 2));
        return;
    }
    counts[0] = output->stats.healthy_plot_count;
    counts[1] = output->stats.water_deficit_plot_count;
    counts[2] = output->stats.disease_plot_count[0];
    counts[3] = output->stats.disease_plot_count[1];
    counts[4] = output->stats.disease_plot_count[2];
    counts[5] = output->stats.disease_plot_count[3];
    names[0] = "\345\201\245\345\272\267"; /* 健康 */
    names[1] = "\347\274\272\346\260\264"; /* 缺水 */
    names[2] = "\346\236\257\350\220\216\347\227\205"; /* 枯萎病 */
    names[3] = "\351\224\210\347\227\205"; /* 锈病 */
    names[4] = "\350\231\253\345\256\263"; /* 虫害 */
    names[5] = "\351\234\211\347\227\205"; /* 霉病 */
    colors[0] = lv_palette_main(LV_PALETTE_GREEN);
    colors[1] = lv_palette_main(LV_PALETTE_BLUE);
    colors[2] = lv_palette_main(LV_PALETTE_RED);
    colors[3] = lv_palette_main(LV_PALETTE_ORANGE);
    colors[4] = lv_palette_main(LV_PALETTE_AMBER);
    colors[5] = lv_palette_main(LV_PALETTE_PURPLE);
    total = output->plot_count;
    disease_total = (uint16_t)(counts[2] + counts[3] + counts[4] + counts[5]);
    label = lv_label_create(popup);
    (void)snprintf(text,
                   sizeof(text),
                   "\346\200\273\345\234\260\345\235\227: %u  \345\201\245\345\272\267:%u  \347\274\272\346\260\264:%u  \347\227\205\345\256\263:%u", /* 总地块: %u  健康:%u  缺水:%u  病害:%u */
                   total,
                   counts[0],
                   counts[1],
                   disease_total);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, 24, 54);
    for (i = 0U; i < 6U; i++) {
        uint16_t pct_x100;
        lv_coord_t y;
        y = (lv_coord_t)(92 + (lv_coord_t)i * 38);
        pct_x100 = (uint16_t)(((uint32_t)counts[i] * 10000UL) / (uint32_t)total);
        label = lv_label_create(popup);
        (void)snprintf(text,
                       sizeof(text),
                       "%s %u\345\235\227 %u.%02u%%", /* %s %u块 %u.%02u%% */
                       names[i],
                       counts[i],
                       pct_x100 / 100U,
                       pct_x100 % 100U);
        lv_label_set_text(label, text);
        lv_obj_set_width(label, 168);
        lv_obj_set_pos(label, 24, (lv_coord_t)(y - 1));
        bar = lv_bar_create(popup);
        lv_obj_set_size(bar, (lv_coord_t)(popup_w - 215), 24);
        lv_obj_set_pos(bar, 196, y);
        lv_bar_set_range(bar, 0, (int32_t)total);
        lv_bar_set_value(bar, counts[i], LV_ANIM_OFF);
        lv_obj_set_style_radius(bar, 5, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, 5, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(bar, lv_palette_lighten(LV_PALETTE_GREY, 2), LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, colors[i], LV_PART_INDICATOR);
    }
    label = lv_label_create(popup);
    lv_label_set_text(label, "\344\275\234\344\270\232\344\274\230\345\205\210\347\272\247"); /* 作业优先级 */
    ui_apply_section_font(label);
    lv_obj_set_pos(label, 24, 320);
    priority_rank = 1U;
    (void)memset(used_disease, 0, sizeof(used_disease));
    for (i = 0U; (i < 3U) && (priority_rank <= 3U); i++) {
        uint16_t j;
        uint16_t best_index;
        uint16_t best_count;
        uint16_t best_severity;
        const monitor_region_t *best_region;
        best_index = APP_MAX_DISEASE_TYPE_COUNT;
        best_count = 0U;
        best_severity = 0U;
        best_region = 0;
        for (j = 0U; j < APP_MAX_DISEASE_TYPE_COUNT; j++) {
            const monitor_region_t *region;
            uint16_t count;
            uint16_t severity;
            if (used_disease[j] != 0U) {
                continue;
            }
            count = output->stats.disease_plot_count[j];
            if (count == 0U) {
                continue;
            }
            region = ui_find_monitor_region(output,
                                            SPRAY_CONTENT_PESTICIDE,
                                            (disease_type_t)(j + 1U));
            severity = (region != 0) ? region->severity_x100 : 100U;
            if ((best_index >= APP_MAX_DISEASE_TYPE_COUNT) ||
                (severity > best_severity) ||
                ((severity == best_severity) && (count > best_count))) {
                best_index = j;
                best_count = count;
                best_severity = severity;
                best_region = region;
            }
        }
        if (best_index >= APP_MAX_DISEASE_TYPE_COUNT) {
            break;
        }
        used_disease[best_index] = 1U;
        label = lv_label_create(popup);
        (void)snprintf(text,
                       sizeof(text),
                       "\344\274\230\345\205\210\347\272\247%s\357\274\232%s  %s  %u\345\235\227  \345\217\230\351\207\217\345\200\215\347\216\207%u%%", /* 优先级%s：%s  %s  %u块  变量倍率%u%% */
                       ui_cn_number_name(priority_rank),
                       ui_disease_name((disease_type_t)(best_index + 1U)),
                       ui_severity_name(best_severity),
                       best_count,
                       (best_region != 0) ? best_region->severity_x100 : best_severity);
        lv_label_set_text(label, text);
        lv_obj_set_width(label, (lv_coord_t)(popup_w - 48));
        lv_obj_set_pos(label, 24, (lv_coord_t)(348 + (lv_coord_t)(priority_rank - 1U) * 22));
        priority_rank++;
    }
    if ((priority_rank <= 3U) && (counts[1] > 0U)) {
        const monitor_region_t *water_region;
        uint16_t water_severity;
        water_region = ui_find_monitor_region(output, SPRAY_CONTENT_WATER, DISEASE_TYPE_NONE);
        water_severity = (water_region != 0) ? water_region->severity_x100 : 100U;
        label = lv_label_create(popup);
        (void)snprintf(text,
                       sizeof(text),
                       "\344\274\230\345\205\210\347\272\247%s\357\274\232\347\274\272\346\260\264  %s  %u\345\235\227  \345\217\230\351\207\217\345\200\215\347\216\207%u%%", /* 优先级%s：缺水  %s  %u块  变量倍率%u%% */
                       ui_cn_number_name(priority_rank),
                       ui_severity_name(water_severity),
                       counts[1],
                       water_severity);
        lv_label_set_text(label, text);
        lv_obj_set_width(label, (lv_coord_t)(popup_w - 48));
        lv_obj_set_pos(label, 24, (lv_coord_t)(348 + (lv_coord_t)(priority_rank - 1U) * 22));
        priority_rank++;
    }
    if (priority_rank == 1U) {
        label = lv_label_create(popup);
        lv_label_set_text(label, "\344\275\234\347\211\251\347\212\266\346\200\201\350\211\257\345\245\275\357\274\214\346\227\240\351\234\200\351\207\215\347\202\271\345\244\204\347\220\206"); /* 作物状态良好，无需重点处理 */
        lv_obj_set_pos(label, 24, 348);
    } else {
        label = lv_label_create(popup);
        lv_label_set_text(label, "\345\273\272\350\256\256\345\205\210\345\244\204\347\220\206\351\207\215\345\272\246\347\227\205\345\256\263\357\274\214\345\206\215\345\244\204\347\220\206\347\274\272\346\260\264\345\234\260\345\235\227"); /* 建议先处理重度病害，再处理缺水地块 */
        lv_obj_set_width(label, (lv_coord_t)(popup_w - 48));
        lv_obj_set_pos(label, 24, 406);
    }
}

static const char *ui_cn_number_name(uint16_t value)
{
    static const char *names[] = {
        "",
        "\344\270\200", /* 一 */
        "\344\272\214", /* 二 */
        "\344\270\211", /* 三 */
        "\345\233\233", /* 四 */
        "\344\272\224", /* 五 */
        "\345\205\255", /* 六 */
        "\344\270\203", /* 七 */
        "\345\205\253", /* 八 */
        "\344\271\235", /* 九 */
        "\345\215\201", /* 十 */
        "\345\215\201\344\270\200", /* 十一 */
        "\345\215\201\344\272\214", /* 十二 */
        "\345\215\201\344\270\211", /* 十三 */
        "\345\215\201\345\233\233", /* 十四 */
        "\345\215\201\344\272\224", /* 十五 */
        "\345\215\201\345\205\255", /* 十六 */
        "\345\215\201\344\270\203", /* 十七 */
        "\345\215\201\345\205\253", /* 十八 */
        "\345\215\201\344\271\235", /* 十九 */
        "\344\272\214\345\215\201" /* 二十 */
    };
    if ((value > 0U) && (value < (uint16_t)(sizeof(names) / sizeof(names[0])))) {
        return names[value];
    }
    return 0;
}

void ui_button_event_cb(lv_event_t *e)
{
    lv_obj_t *target;
    app_result_t result;
    mix_input_t mix_input;
    /* UI 只集输入并调用 app_core，不直接改业务输*/
    target = lv_event_get_target(e);
    if (target == g_ui_ctx.button_init) {
        ui_monitor_run(UI_MONITOR_ACTION_INIT);
    } else if (target == g_ui_ctx.button_auto) {
        ui_monitor_run(UI_MONITOR_ACTION_AUTO);
    } else if (target == g_ui_ctx.button_stats) {
        ui_monitor_show_stats_popup();
    } else if (target == g_ui_ctx.button_manual_apply) {
        ui_run_manual_apply();
    } else if (target == g_ui_ctx.button_mix_run) {
        /* 配药成功后清掉依赖旧配药结果的页*/
        mix_input.tank_capacity_ml_x10 = ui_mix_selected_capacity_ml_x10();
        result = app_core_run_mix_with_input(&mix_input);
        ui_mix_update_view(result);
        if (result == APP_OK) {
            ui_plan_update_view(APP_ERR_STATE);
            ui_sim_update_view(APP_ERR_STATE);
            ui_switch_page(UI_PAGE_MIX);
        }
    } else if (target == g_ui_ctx.button_plan_run) {
        /* 规划页会重建较重的路线内*/
        if (g_ui_ctx.current_page == UI_PAGE_MIX) {
            ui_mix_release_visual_content();
        }
        result = app_core_run_plan();
        if (result == APP_OK) {
            ui_switch_page(UI_PAGE_PLAN);
        } else {
            if (g_ui_ctx.current_page == UI_PAGE_MIX) {
                ui_mix_reload_view();
            }
            ui_plan_update_view(result);
            ui_sim_update_view(APP_ERR_STATE);
        }
    } else if (target == g_ui_ctx.button_sim_start) {
        /* 累加仿真前确保地图和进度控件存在 */
        if (g_ui_ctx.current_page == UI_PAGE_PLAN) {
            ui_plan_release_heavy_text();
        }
        ui_sim_ensure_page_content();
        result = app_core_start_sim();
        ui_sim_update_view(result);
        if (result == APP_OK) {
            ui_switch_page(UI_PAGE_SIM);
        }
    } else if (target == g_ui_ctx.button_sim_stop) {
        result = app_core_stop_sim();
        ui_sim_update_view(result);
        ui_switch_page(UI_PAGE_SIM);
    } else if (target == g_ui_ctx.button_sim_recall) {
        result = app_core_trigger_recall();
        ui_sim_update_view(result);
    } else if (target == g_ui_ctx.button_sim_resume) {
        result = app_core_resume_from_refill();
        ui_sim_update_view(result);
    } else if (target == g_ui_ctx.button_sim_speed) {
        char speed_text[24];
        /* 倍只更新仿真节规划和按终*/
        (void)sim_module_cycle_time_scale();
        (void)snprintf(speed_text, sizeof(speed_text), "\345\200\215\351\200\237%ux", sim_module_get_time_scale()); /* 倍速%ux */
        if (g_ui_ctx.sim_speed_label != 0) {
            lv_label_set_text(g_ui_ctx.sim_speed_label, speed_text);
            lv_obj_center(g_ui_ctx.sim_speed_label);
        }
        ui_sim_update_view(ui_sim_current_view_result());
    }
}

/* 创建只通业务按*/
lv_obj_t *ui_create_action_button(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *btn;
    lv_obj_t *label;
    /* 创建业务操作按钮，所有点击统交给 ui_button_event_cb 处理 */
    btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 110, 42);
    lv_obj_set_pos(btn, x, y);
    lv_obj_add_event_cb(btn, ui_button_event_cb, LV_EVENT_CLICKED, 0);
    /* 创建按钮文字标标签，放在按*/
    label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

/* 创建手动输入键盘上的按钮 */
static lv_obj_t *ui_create_keypad_button(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *btn;
    lv_obj_t *label;
    /* 创建数字键盘按钮，用于输入地块编*/
    btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 60, 36);
    lv_obj_set_pos(btn, x, y);
    lv_obj_add_event_cb(btn, ui_manual_keypad_btn_event_cb, LV_EVENT_CLICKED, 0);
    /* 创建键盘按钮上的数字/功能文字 */
    label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

/* 点击地块编号输入框时显示数字键盘 */
static void ui_manual_plot_id_event_cb(lv_event_t *e)
{
    lv_event_code_t code;
    code = lv_event_get_code(e);
    if ((code == LV_EVENT_FOCUSED) || (code == LV_EVENT_CLICKED)) {
        if (g_ui_ctx.manual_keyboard != 0) {
            lv_obj_move_foreground(g_ui_ctx.manual_keyboard);
            lv_obj_clear_flag(g_ui_ctx.manual_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* 键盘容器色不状态理事件，保留回调入口 */
static void ui_manual_keyboard_event_cb(lv_event_t *e)
{
    (void)e;
}

/* 处理数字键盘按键 */
static void ui_manual_keypad_btn_event_cb(lv_event_t *e)
{
    lv_obj_t *target;
    lv_obj_t *label;
    const char *key_text;
    const char *current_text;
    char next_text[8];
    uint16_t len;
    /* 先取出按钮的文字作为键值 */
    target = lv_event_get_target(e);
    label = lv_obj_get_child(target, 0);
    key_text = lv_label_get_text(label);
    current_text = lv_textarea_get_text(g_ui_ctx.manual_plot_id_ta);
    /* 键值和删除按键处理 */
    if (strcmp(key_text, "\347\241\256\345\256\232") == 0) { /* 确定 */
        lv_obj_add_flag(g_ui_ctx.manual_keyboard, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (strcmp(key_text, "\345\210\240\351\231\244") == 0) { /* 删除 */
        lv_textarea_del_char(g_ui_ctx.manual_plot_id_ta);
        return;
    }
    /* 宽数字追加到当前输入后面 */
    len = (uint16_t)strlen(current_text);
    if (len >= sizeof(next_text) - 1U) {
        return;
    }
    /* 单个 0 头时用新数字替换 */
    if ((len == 1U) && (current_text[0] == '0')) {
        next_text[0] = key_text[0];
        next_text[1] = '\0';
    } else {
        (void)strcpy(next_text, current_text);
        next_text[len] = key_text[0];
        next_text[len + 1U] = '\0';
    }
    lv_textarea_set_text(g_ui_ctx.manual_plot_id_ta, next_text);
}

/* 按需创建仿真页的控制区和地图*/
void ui_sim_ensure_page_content(void)
{
    lv_obj_t *panel_label;
    lv_obj_t *sim_panel_left;
    lv_obj_t *sim_panel_right;
    const monitor_output_t *monitor_output;
    const plan_output_t *plan_output;
    ui_sim_grid_layout_t layout;
    uint32_t i;
    /* 地图对象较路径，只在进入仿真页时创*/
    if ((g_ui_ctx.sim_page == 0) || (g_ui_ctx.sim_state_label != 0)) {
        return;
    }
    /* 创建仿真页左侧控制面板，放启停启动、和进度信息 */
    sim_panel_left = lv_obj_create(g_ui_ctx.sim_page);
    lv_obj_set_size(sim_panel_left, 320, 500);
    lv_obj_set_pos(sim_panel_left, 12, 12);
    lv_obj_set_scroll_dir(sim_panel_left, LV_DIR_VER);
    /* 创建左侧面板标标题 */
    panel_label = lv_label_create(sim_panel_left);
    lv_label_set_text(panel_label, "\344\273\277\347\234\237\346\216\247\345\210\266"); /* 仿真控制 */
    ui_apply_section_font(panel_label);
    lv_obj_set_pos(panel_label, 8, 4);
    /* 创建仿真状标签，显示运行状*/
    g_ui_ctx.sim_state_label = lv_label_create(sim_panel_left);
    lv_obj_set_width(g_ui_ctx.sim_state_label, 292);
    lv_obj_set_pos(g_ui_ctx.sim_state_label, 8, 28);
    lv_label_set_text(g_ui_ctx.sim_state_label, "\344\273\277\347\234\237\347\212\266\346\200\201\357\274\232\347\251\272\351\227\262"); /* 仿真状态：空闲 */
    /* 创建累加仿真按钮 */
    g_ui_ctx.button_sim_start = lv_btn_create(sim_panel_left);
    lv_obj_set_size(g_ui_ctx.button_sim_start, 88, 34);
    lv_obj_set_pos(g_ui_ctx.button_sim_start, 8, 74);
    lv_obj_add_event_cb(g_ui_ctx.button_sim_start, ui_button_event_cb, LV_EVENT_CLICKED, 0);
    /* 创建累加按钮文字标标签 */
    panel_label = lv_label_create(g_ui_ctx.button_sim_start);
    lv_label_set_text(panel_label, "\345\220\257\345\212\250\344\273\277\347\234\237"); /* 启动仿真 */
    lv_obj_center(panel_label);
    /* 创建停启动仿真按钮 */
    g_ui_ctx.button_sim_stop = lv_btn_create(sim_panel_left);
    lv_obj_set_size(g_ui_ctx.button_sim_stop, 88, 34);
    lv_obj_set_pos(g_ui_ctx.button_sim_stop, 106, 74);
    lv_obj_add_event_cb(g_ui_ctx.button_sim_stop, ui_button_event_cb, LV_EVENT_CLICKED, 0);
    /* 创建停启动按钮文字标标签 */
    panel_label = lv_label_create(g_ui_ctx.button_sim_stop);
    lv_label_set_text(panel_label, "\345\201\234\346\255\242\344\273\277\347\234\237"); /* 停止仿真 */
    lv_obj_center(panel_label);
    /* 创建仿真倍切换按*/
    g_ui_ctx.button_sim_speed = lv_btn_create(sim_panel_left);
    lv_obj_set_size(g_ui_ctx.button_sim_speed, 96, 34);
    lv_obj_set_pos(g_ui_ctx.button_sim_speed, 204, 74);
    lv_obj_add_event_cb(g_ui_ctx.button_sim_speed, ui_button_event_cb, LV_EVENT_CLICKED, 0);
    /* 创建倍按终字标签，显示当前倍*/
    g_ui_ctx.sim_speed_label = lv_label_create(g_ui_ctx.button_sim_speed);
    {
        char speed_text[24];
        (void)snprintf(speed_text, sizeof(speed_text), "\345\200\215\351\200\237%ux", sim_module_get_time_scale()); /* 倍速%ux */
        lv_label_set_text(g_ui_ctx.sim_speed_label, speed_text);
    }
    lv_obj_center(g_ui_ctx.sim_speed_label);
    g_ui_ctx.button_sim_recall = lv_btn_create(sim_panel_left);
    lv_obj_set_size(g_ui_ctx.button_sim_recall, 136, 34);
    lv_obj_set_pos(g_ui_ctx.button_sim_recall, 8, 114);
    lv_obj_add_event_cb(g_ui_ctx.button_sim_recall, ui_button_event_cb, LV_EVENT_CLICKED, 0);
    panel_label = lv_label_create(g_ui_ctx.button_sim_recall);
    lv_label_set_text(panel_label, "\346\211\213\345\212\250\350\277\224\345\233\236"); /* 手动返回 */
    lv_obj_center(panel_label);
    g_ui_ctx.button_sim_resume = lv_btn_create(sim_panel_left);
    lv_obj_set_size(g_ui_ctx.button_sim_resume, 150, 34);
    lv_obj_set_pos(g_ui_ctx.button_sim_resume, 150, 114);
    lv_obj_add_event_cb(g_ui_ctx.button_sim_resume, ui_button_event_cb, LV_EVENT_CLICKED, 0);
    panel_label = lv_label_create(g_ui_ctx.button_sim_resume);
    lv_label_set_text(panel_label, "\350\265\267\351\243\236\347\273\247\347\273\255"); /* 起飞继续 */
    lv_obj_center(panel_label);
    /* 创建药进度文字标标签 */
    g_ui_ctx.sim_progress_label = lv_label_create(sim_panel_left);
    lv_obj_set_width(g_ui_ctx.sim_progress_label, 292);
    lv_obj_set_pos(g_ui_ctx.sim_progress_label, 8, 158);
    lv_label_set_text(g_ui_ctx.sim_progress_label, "\350\267\257\347\272\277\350\277\233\345\272\246\357\274\2320.00%"); /* 路线进度：0.00% */
    /* 创建药进度*/
    g_ui_ctx.sim_progress_bar = lv_bar_create(sim_panel_left);
    lv_obj_set_size(g_ui_ctx.sim_progress_bar, 292, 18);
    lv_obj_set_pos(g_ui_ctx.sim_progress_bar, 8, 182);
    lv_bar_set_range(g_ui_ctx.sim_progress_bar, 0, 10000);
    lv_bar_set_value(g_ui_ctx.sim_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(g_ui_ctx.sim_progress_bar, 9, LV_PART_MAIN);
    lv_obj_set_style_radius(g_ui_ctx.sim_progress_bar, 9, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_ui_ctx.sim_progress_bar, lv_palette_lighten(LV_PALETTE_GREY, 2), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_ui_ctx.sim_progress_bar, lv_palette_main(LV_PALETTE_GREEN), LV_PART_INDICATOR);
    g_ui_ctx.sim_battery_label = lv_label_create(sim_panel_left);
    lv_obj_set_width(g_ui_ctx.sim_battery_label, 292);
    lv_obj_set_pos(g_ui_ctx.sim_battery_label, 8, 208);
    lv_label_set_text(g_ui_ctx.sim_battery_label, "\347\224\265\351\207\217\357\274\2320.00%"); /* 电量：0.00% */
    g_ui_ctx.sim_battery_bar = lv_bar_create(sim_panel_left);
    lv_obj_set_size(g_ui_ctx.sim_battery_bar, 292, 18);
    lv_obj_set_pos(g_ui_ctx.sim_battery_bar, 8, 232);
    lv_bar_set_range(g_ui_ctx.sim_battery_bar, 0, 10000);
    lv_bar_set_value(g_ui_ctx.sim_battery_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(g_ui_ctx.sim_battery_bar, 9, LV_PART_MAIN);
    lv_obj_set_style_radius(g_ui_ctx.sim_battery_bar, 9, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_ui_ctx.sim_battery_bar, lv_palette_lighten(LV_PALETTE_GREY, 2), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_ui_ctx.sim_battery_bar, lv_palette_main(LV_PALETTE_GREEN), LV_PART_INDICATOR);
    /* 创建仿真执行概已选标标签 */
    g_ui_ctx.sim_summary_label = lv_label_create(sim_panel_left);
    lv_label_set_long_mode(g_ui_ctx.sim_summary_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_ui_ctx.sim_summary_label, 292);
    lv_obj_set_pos(g_ui_ctx.sim_summary_label, 8, 262);
    lv_label_set_text(g_ui_ctx.sim_summary_label, "\346\227\240\344\273\277\347\234\237\346\225\260\346\215\256"); /* 无仿真数据 */
    /* 创建仿真页右侧面板，放地图当前任务和报警信息 */
    sim_panel_right = lv_obj_create(g_ui_ctx.sim_page);
    lv_obj_set_size(sim_panel_right, 680, 500);
    lv_obj_set_pos(sim_panel_right, 344, 12);
    lv_obj_set_scroll_dir(sim_panel_right, LV_DIR_VER);
    /* 创建仿真地图容器，地块服务点和普通机图标都放在这里 */
    g_ui_ctx.sim_grid_panel = lv_obj_create(sim_panel_right);
    lv_obj_set_size(g_ui_ctx.sim_grid_panel, 640, 260);
    lv_obj_set_pos(g_ui_ctx.sim_grid_panel, 8, 8);
    lv_obj_clear_flag(g_ui_ctx.sim_grid_panel, LV_OBJ_FLAG_SCROLLABLE);
    monitor_output = app_core_get_monitor_output();
    plan_output = app_core_get_plan_output();
    if ((monitor_output->plot_count > 0U) &&
        (monitor_output->grid.rows > 0U) &&
        (monitor_output->grid.cols > 0U) &&
        (monitor_output->grid.cell_width_mm > 0U) &&
        (monitor_output->grid.cell_height_mm > 0U)) {
        ui_sim_calc_grid_layout(monitor_output, plan_output, &layout);
        /* 地块底图控制逻辑，运行中移动飞机图标 */
        for (i = 0U; i < monitor_output->plot_count; i++) {
            lv_obj_t *cell;
            lv_coord_t cell_x;
            lv_coord_t cell_y;
            lv_coord_t cell_x2;
            lv_coord_t cell_y2;
            lv_coord_t plot_w;
            lv_coord_t plot_h;
            cell_x = ui_sim_map_x(&layout, monitor_output->plot_status[i].x_mm);
            cell_y = ui_sim_map_y(&layout, monitor_output->plot_status[i].y_mm);
            cell_x2 = ui_sim_map_x(&layout,
                                   monitor_output->plot_status[i].x_mm +
                                       (int32_t)monitor_output->grid.cell_width_mm);
            cell_y2 = ui_sim_map_y(&layout,
                                   monitor_output->plot_status[i].y_mm +
                                       (int32_t)monitor_output->grid.cell_height_mm);
            plot_w = cell_x2 - cell_x;
            plot_h = cell_y2 - cell_y;
            if (plot_w < 4) {
                plot_w = 4;
            }
            if (plot_h < 4) {
                plot_h = 4;
            }
            /* 创建仿真地图上的单个地块底图 */
            cell = lv_obj_create(g_ui_ctx.sim_grid_panel);
            lv_obj_set_size(cell, plot_w - 2, plot_h - 2);
            lv_obj_set_pos(cell, cell_x, cell_y);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(cell, 1, LV_PART_MAIN);
            lv_obj_set_style_border_width(cell, 1, LV_PART_MAIN);
            lv_obj_set_style_border_color(cell, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_MAIN);
            lv_obj_set_style_bg_color(cell, ui_plot_color(&monitor_output->plot_status[i]), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, LV_PART_MAIN);
            g_ui_ctx.sim_grid_cell[i] = cell;
        }
        if (plan_output->waypoint_count > 0U) {
            lv_coord_t service_x;
            lv_coord_t service_y;
            lv_obj_t *service_icon;
            service_x = ui_sim_map_x(&layout, plan_output->summary.service_point.x_mm);
            service_y = ui_sim_map_y(&layout, plan_output->summary.service_point.y_mm);
            /* 创建固定服务点图标，表示起普通、续液和返航位置 */
            service_icon = lv_obj_create(g_ui_ctx.sim_grid_panel);
            lv_obj_set_size(service_icon, 28, 28);
            lv_obj_set_pos(service_icon, service_x - 14, service_y - 14);
            lv_obj_clear_flag(service_icon, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_bg_color(service_icon, lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(service_icon, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_color(service_icon, lv_color_black(), LV_PART_MAIN);
            lv_obj_set_style_border_width(service_icon, 1, LV_PART_MAIN);
            lv_obj_set_style_radius(service_icon, 14, LV_PART_MAIN);
            /* 创建服务点图标内的文*/
            panel_label = lv_label_create(service_icon);
            lv_label_set_text(panel_label, "\350\265\267"); /* 起 */
            lv_obj_center(panel_label);
        }
    }
    /* 创建飞机图标，运行时航点动这控制对*/
    g_ui_ctx.sim_grid_plane_icon = lv_obj_create(g_ui_ctx.sim_grid_panel);
    lv_obj_set_size(g_ui_ctx.sim_grid_plane_icon, 22, 22);
    lv_obj_clear_flag(g_ui_ctx.sim_grid_plane_icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(g_ui_ctx.sim_grid_plane_icon, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_ui_ctx.sim_grid_plane_icon, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_ui_ctx.sim_grid_plane_icon, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(g_ui_ctx.sim_grid_plane_icon, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(g_ui_ctx.sim_grid_plane_icon, 11, LV_PART_MAIN);
    lv_obj_add_flag(g_ui_ctx.sim_grid_plane_icon, LV_OBJ_FLAG_HIDDEN);
    /* 创建飞机图标内的文字 */
    panel_label = lv_label_create(g_ui_ctx.sim_grid_plane_icon);
    lv_label_set_text(panel_label, "\346\234\272"); /* 机 */
    lv_obj_center(panel_label);
    /* 创建当前任务标标签，显示当前航点动作和坐标 */
    g_ui_ctx.sim_current_label = lv_label_create(sim_panel_right);
    lv_label_set_long_mode(g_ui_ctx.sim_current_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_ui_ctx.sim_current_label, 640);
    lv_obj_set_pos(g_ui_ctx.sim_current_label, 8, 284);
    lv_label_set_text(g_ui_ctx.sim_current_label, "\345\275\223\345\211\215\350\210\252\347\202\271\n\346\227\240"); /* 当前航点
无 */
    /* 创建仿真报警标标签 */
    g_ui_ctx.sim_alarm_label = lv_label_create(sim_panel_right);
    lv_label_set_long_mode(g_ui_ctx.sim_alarm_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_ui_ctx.sim_alarm_label, 640);
    lv_obj_set_pos(g_ui_ctx.sim_alarm_label, 8, 432);
    lv_label_set_text(g_ui_ctx.sim_alarm_label, "\346\212\245\350\255\246\n\346\227\240"); /* 报警
无 */
}

/* 释放仿真页的地图和控制对*/
void ui_sim_release_page_content(void)
{
    while ((g_ui_ctx.sim_page != 0) && (lv_obj_get_child_cnt(g_ui_ctx.sim_page) > 0U)) {
        lv_obj_del(lv_obj_get_child(g_ui_ctx.sim_page, 0));
    }
    g_ui_ctx.sim_state_label = 0;
    g_ui_ctx.sim_progress_label = 0;
    g_ui_ctx.sim_progress_bar = 0;
    g_ui_ctx.sim_battery_label = 0;
    g_ui_ctx.sim_battery_bar = 0;
    g_ui_ctx.sim_summary_label = 0;
    g_ui_ctx.sim_grid_panel = 0;
    g_ui_ctx.sim_grid_plane_icon = 0;
    (void)memset(g_ui_ctx.sim_grid_cell, 0, sizeof(g_ui_ctx.sim_grid_cell));
    g_ui_ctx.sim_current_label = 0;
    g_ui_ctx.sim_alarm_label = 0;
    g_ui_ctx.button_sim_start = 0;
    g_ui_ctx.button_sim_stop = 0;
    g_ui_ctx.button_sim_speed = 0;
    g_ui_ctx.button_sim_recall = 0;
    g_ui_ctx.button_sim_resume = 0;
    g_ui_ctx.sim_speed_label = 0;
}

/* 顶部导航按钮事件 */
void ui_nav_event_cb(lv_event_t *e)
{
    lv_obj_t *target;
    /* 导航控制页面，不主动运行算法 */
    target = lv_event_get_target(e);
    if (target == g_ui_ctx.nav_monitor_btn) {
        ui_switch_page(UI_PAGE_MONITOR);
    } else if (target == g_ui_ctx.nav_mix_btn) {
        ui_switch_page(UI_PAGE_MIX);
    } else if (target == g_ui_ctx.nav_plan_btn) {
        ui_switch_page(UI_PAGE_PLAN);
    } else if (target == g_ui_ctx.nav_sim_btn) {
        if (g_ui_ctx.current_page == UI_PAGE_PLAN) {
            ui_plan_release_heavy_text();
        }
        ui_sim_ensure_page_content();
        ui_sim_update_view(ui_sim_current_view_result());
        ui_switch_page(UI_PAGE_SIM);
    }
}

/* 初监测UI 上下文和默监测页面状*/
void ui_module_init(void)
{
    (void)memset(&g_ui_ctx, 0, sizeof(g_ui_ctx));
    g_ui_ctx.selected_plot_index = 0U;
    g_ui_ctx.selected_plan_sub_batch_index = 0U;
    g_ui_ctx.current_page = UI_PAGE_MONITOR;
    ui_monitor_set_default_input();
}

/* 创建四个主页面的容器以及状态标签控件 */
void ui_module_load_home(void)
{
    lv_obj_t *panel_label;
    lv_coord_t screen_w;
    lv_coord_t screen_h;
    lv_coord_t content_y;
    lv_coord_t info_w;
    lv_coord_t info_h;
    lv_coord_t manual_panel_w;
    lv_coord_t manual_panel_h;
    lv_coord_t auto_panel_w;
    lv_coord_t grid_x;
    lv_coord_t grid_y;
    lv_coord_t grid_w;
    lv_coord_t grid_h;
    lv_coord_t legend_w;
    lv_obj_t *legend_panel;
    lv_obj_t *mix_panel_left;
    lv_obj_t *mix_panel_right;
    lv_obj_t *plan_header_panel;
    lv_obj_t *plan_panel_mid;
    lv_obj_t *plan_panel_right;
    /* 这里创建基状态框架，重资源内容切页时按重建 */
    /* 创建根屏幕，有页面和导航都挂在这screen */
    g_ui_ctx.screen = lv_obj_create(0);
    lv_obj_clear_flag(g_ui_ctx.screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(g_ui_ctx.screen, &lv_font_chinese_18, 0);
    screen_w = lv_disp_get_hor_res(0);
    screen_h = lv_disp_get_ver_res(0);
    info_w = 300;
    content_y = 140;
    info_h = 220;
    manual_panel_w = (screen_w >= 1000) ? 660 : 600;
    manual_panel_h = 130;
    auto_panel_w = (screen_w >= 1000) ? 332 : 300;
    legend_w = 160;
    grid_x = info_w + 24;
    grid_y = content_y;
    grid_w = (lv_coord_t)(screen_w - grid_x - legend_w - 28);
    grid_h = (lv_coord_t)(screen_h - grid_y - 102);
    if (grid_h < info_h) {
        grid_h = info_h;
    }
    /* 创建顶部系统标标题标标签 */
    g_ui_ctx.title_label = lv_label_create(g_ui_ctx.screen);
    lv_label_set_text(g_ui_ctx.title_label, "\345\206\234\347\224\260\346\244\215\344\277\235\344\273\277\347\234\237\347\263\273\347\273\237"); /* 农田植保仿真系统 */
    ui_apply_title_font(g_ui_ctx.title_label);
    lv_obj_set_pos(g_ui_ctx.title_label, 12, 8);
    /* 创建顶部状态标签，用来显示当前业务阶段 */
    g_ui_ctx.state_label = lv_label_create(g_ui_ctx.screen);
    lv_label_set_text(g_ui_ctx.state_label, "\347\263\273\347\273\237\347\212\266\346\200\201\357\274\232\347\251\272\351\227\262"); /* 系统状态：空闲 */
    ui_apply_section_font(g_ui_ctx.state_label);
    lv_obj_set_pos(g_ui_ctx.state_label, 12, 30);
    /* 创建顶部“监测服务只*/
    g_ui_ctx.nav_monitor_btn = lv_btn_create(g_ui_ctx.screen);
    lv_obj_set_size(g_ui_ctx.nav_monitor_btn, 88, 32);
    lv_obj_set_pos(g_ui_ctx.nav_monitor_btn, 632, 12);
    lv_obj_add_event_cb(g_ui_ctx.nav_monitor_btn, ui_nav_event_cb, LV_EVENT_CLICKED, 0);
    /* 创建“监测服务只终*/
    panel_label = lv_label_create(g_ui_ctx.nav_monitor_btn);
    lv_label_set_text(panel_label, "\347\233\221\346\265\213"); /* 监测 */
    ui_apply_section_font(panel_label);
    lv_obj_center(panel_label);
    /* 创建顶部“配起飞服务只*/
    g_ui_ctx.nav_mix_btn = lv_btn_create(g_ui_ctx.screen);
    lv_obj_set_size(g_ui_ctx.nav_mix_btn, 88, 32);
    lv_obj_set_pos(g_ui_ctx.nav_mix_btn, 728, 12);
    lv_obj_add_event_cb(g_ui_ctx.nav_mix_btn, ui_nav_event_cb, LV_EVENT_CLICKED, 0);
    /* 创建“配起飞服务只终*/
    panel_label = lv_label_create(g_ui_ctx.nav_mix_btn);
    lv_label_set_text(panel_label, "\351\205\215\350\215\257"); /* 配药 */
    ui_apply_section_font(panel_label);
    lv_obj_center(panel_label);
    /* 创建顶部“规划服务只*/
    g_ui_ctx.nav_plan_btn = lv_btn_create(g_ui_ctx.screen);
    lv_obj_set_size(g_ui_ctx.nav_plan_btn, 88, 32);
    lv_obj_set_pos(g_ui_ctx.nav_plan_btn, 824, 12);
    lv_obj_add_event_cb(g_ui_ctx.nav_plan_btn, ui_nav_event_cb, LV_EVENT_CLICKED, 0);
    /* 创建“规划服务只终*/
    panel_label = lv_label_create(g_ui_ctx.nav_plan_btn);
    lv_label_set_text(panel_label, "\350\247\204\345\210\222"); /* 规划 */
    ui_apply_section_font(panel_label);
    lv_obj_center(panel_label);
    /* 创建顶部“仿真服务只*/
    g_ui_ctx.nav_sim_btn = lv_btn_create(g_ui_ctx.screen);
    lv_obj_set_size(g_ui_ctx.nav_sim_btn, 88, 32);
    lv_obj_set_pos(g_ui_ctx.nav_sim_btn, 920, 12);
    lv_obj_add_event_cb(g_ui_ctx.nav_sim_btn, ui_nav_event_cb, LV_EVENT_CLICKED, 0);
    /* 创建“仿真服务只终*/
    panel_label = lv_label_create(g_ui_ctx.nav_sim_btn);
    lv_label_set_text(panel_label, "\344\273\277\347\234\237"); /* 仿真 */
    ui_apply_section_font(panel_label);
    lv_obj_center(panel_label);
    /* 创建监测页根容器 */
    g_ui_ctx.monitor_page = lv_obj_create(g_ui_ctx.screen);
    lv_obj_set_size(g_ui_ctx.monitor_page, screen_w, screen_h - 52);
    lv_obj_set_pos(g_ui_ctx.monitor_page, 0, 52);
    lv_obj_clear_flag(g_ui_ctx.monitor_page, LV_OBJ_FLAG_SCROLLABLE);
    /* 创建配药页根容器 */
    g_ui_ctx.mix_page = lv_obj_create(g_ui_ctx.screen);
    lv_obj_set_size(g_ui_ctx.mix_page, screen_w, screen_h - 52);
    lv_obj_set_pos(g_ui_ctx.mix_page, 0, 52);
    lv_obj_clear_flag(g_ui_ctx.mix_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_ui_ctx.mix_page, LV_OBJ_FLAG_HIDDEN);
    /* 创建规划页根容器 */
    g_ui_ctx.plan_page = lv_obj_create(g_ui_ctx.screen);
    lv_obj_set_size(g_ui_ctx.plan_page, screen_w, screen_h - 52);
    lv_obj_set_pos(g_ui_ctx.plan_page, 0, 52);
    lv_obj_clear_flag(g_ui_ctx.plan_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_ui_ctx.plan_page, LV_OBJ_FLAG_HIDDEN);
    /* 创建仿真页根容器，具体地图内容按创建 */
    g_ui_ctx.sim_page = lv_obj_create(g_ui_ctx.screen);
    lv_obj_set_size(g_ui_ctx.sim_page, screen_w, screen_h - 52);
    lv_obj_set_pos(g_ui_ctx.sim_page, 0, 52);
    lv_obj_clear_flag(g_ui_ctx.sim_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_ui_ctx.sim_page, LV_OBJ_FLAG_HIDDEN);
    /* 创建监测页自动监测测控制面*/
    g_ui_ctx.auto_panel = lv_obj_create(g_ui_ctx.monitor_page);
    lv_obj_set_size(g_ui_ctx.auto_panel, auto_panel_w, 130);
    lv_obj_set_pos(g_ui_ctx.auto_panel, 12, 0);
    lv_obj_clear_flag(g_ui_ctx.auto_panel, LV_OBJ_FLAG_SCROLLABLE);
    /* 创建控制测面板标*/
    panel_label = lv_label_create(g_ui_ctx.auto_panel);
    lv_label_set_text(panel_label, "\350\207\252\345\212\250\346\243\200\346\265\213"); /* 自动检测 */
    ui_apply_section_font(panel_label);
    lv_obj_set_pos(panel_label, 0 , 0 );
    /* 创建“初始化网格”按药航点成健康地*/
    g_ui_ctx.button_init = ui_create_action_button(g_ui_ctx.auto_panel, "\345\210\235\345\247\213\345\214\226\347\275\221\346\240\274", -2, 24); /* 初始化网格 */
    /* 创建“自动监测测按药生成随机缺水/病害结果 */
    g_ui_ctx.button_auto = ui_create_action_button(g_ui_ctx.auto_panel, "\350\207\252\345\212\250\346\243\200\346\265\213", 112, 24); /* 自动检测 */
    g_ui_ctx.button_stats = lv_btn_create(g_ui_ctx.auto_panel);
    lv_obj_set_size(g_ui_ctx.button_stats, 84, 42);
    lv_obj_set_pos(g_ui_ctx.button_stats, 226, 24);
    lv_obj_add_event_cb(g_ui_ctx.button_stats, ui_button_event_cb, LV_EVENT_CLICKED, 0);
    panel_label = lv_label_create(g_ui_ctx.button_stats);
    lv_label_set_text(panel_label, "\347\273\237\350\256\241\345\233\276"); /* 统计图 */
    lv_obj_center(panel_label);

    /* 创建监测页手动设置面板 */
    g_ui_ctx.manual_panel = lv_obj_create(g_ui_ctx.monitor_page);
    lv_obj_set_size(g_ui_ctx.manual_panel, manual_panel_w, manual_panel_h);
    lv_obj_set_pos(g_ui_ctx.manual_panel, (lv_coord_t)(auto_panel_w + 20), 0);
    lv_obj_clear_flag(g_ui_ctx.manual_panel, LV_OBJ_FLAG_SCROLLABLE);
    /* 创建手动设置面板标标题 */
    panel_label = lv_label_create(g_ui_ctx.manual_panel);
    lv_label_set_text(panel_label, "\346\211\213\345\212\250\350\256\276\347\275\256"); /* 手动设置 */
    ui_apply_section_font(panel_label);
    lv_obj_set_pos(panel_label, 8, 4);
    /* 创建作物及生长期选择下拉框，放置在手动设置面板第二行 */
    panel_label = lv_label_create(g_ui_ctx.manual_panel);
    lv_label_set_text(panel_label, "\344\275\234\347\211\251"); /* 作物 */
    lv_obj_set_pos(panel_label, 8, 74);
    g_ui_ctx.crop_type_dd = lv_dropdown_create(g_ui_ctx.manual_panel);
    lv_dropdown_set_options(g_ui_ctx.crop_type_dd, "\346\260\264\347\250\273\n\345\260\217\351\272\246"); /* 水稻
小麦 */
    lv_obj_set_size(g_ui_ctx.crop_type_dd, 92, 30);
    lv_obj_set_pos(g_ui_ctx.crop_type_dd, 48, 68);
    lv_obj_set_style_pad_top(g_ui_ctx.crop_type_dd, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(g_ui_ctx.crop_type_dd, 8, LV_PART_MAIN);
    panel_label = lv_label_create(g_ui_ctx.manual_panel);
    lv_label_set_text(panel_label, "\347\224\237\351\225\277\346\234\237"); /* 生长期 */
    lv_obj_set_pos(panel_label, 158, 74);
    g_ui_ctx.growth_stage_dd = lv_dropdown_create(g_ui_ctx.manual_panel);
    lv_dropdown_set_options(g_ui_ctx.growth_stage_dd, "\350\213\227\346\234\237\n\346\210\220\351\225\277\346\234\237"); /* 苗期
成长期 */
    lv_obj_set_size(g_ui_ctx.growth_stage_dd, 96, 30);
    lv_obj_set_pos(g_ui_ctx.growth_stage_dd, 224, 68);
    lv_obj_set_style_pad_top(g_ui_ctx.growth_stage_dd, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(g_ui_ctx.growth_stage_dd, 8, LV_PART_MAIN);
    /* 创建“地块编号字段说明标*/
    panel_label = lv_label_create(g_ui_ctx.manual_panel);
    lv_label_set_text(panel_label, "\345\234\260\345\235\227\347\274\226\345\217\267"); /* 地块编号 */
    lv_obj_set_pos(panel_label, 8, 30);
    /* 创建地块编号输入*/
    g_ui_ctx.manual_plot_id_ta = lv_textarea_create(g_ui_ctx.manual_panel);
    lv_obj_set_size(g_ui_ctx.manual_plot_id_ta, 42, 30);
    lv_obj_set_pos(g_ui_ctx.manual_plot_id_ta, 78, 24);
    lv_textarea_set_one_line(g_ui_ctx.manual_plot_id_ta, 1);
    lv_textarea_set_text(g_ui_ctx.manual_plot_id_ta, "1");
    lv_obj_set_style_pad_top(g_ui_ctx.manual_plot_id_ta, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(g_ui_ctx.manual_plot_id_ta, 4, LV_PART_MAIN);
    lv_obj_add_event_cb(g_ui_ctx.manual_plot_id_ta, ui_manual_plot_id_event_cb, LV_EVENT_ALL, 0);
    /* 创建“状态字段说明标*/
    panel_label = lv_label_create(g_ui_ctx.manual_panel);
    lv_label_set_text(panel_label, "\347\212\266\346\200\201"); /* 状态 */
    lv_obj_set_pos(panel_label, 136, 30);
    /* 创建地块状下拉枚举 */
    g_ui_ctx.manual_state_dd = lv_dropdown_create(g_ui_ctx.manual_panel);
    lv_dropdown_set_options(g_ui_ctx.manual_state_dd, "\345\201\245\345\272\267\n\347\274\272\346\260\264\n\347\227\205\345\256\263"); /* 健康
缺水
病害 */
    lv_obj_set_size(g_ui_ctx.manual_state_dd, 94, 30);
    lv_obj_set_pos(g_ui_ctx.manual_state_dd, 176, 24);
    lv_obj_set_style_pad_top(g_ui_ctx.manual_state_dd, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(g_ui_ctx.manual_state_dd, 8, LV_PART_MAIN);
    /* 创建“病害字段说明标*/
    panel_label = lv_label_create(g_ui_ctx.manual_panel);
    lv_label_set_text(panel_label, "\347\227\205\345\256\263"); /* 病害 */
    lv_obj_set_pos(panel_label, 288, 30);
    /* 创建病害类型下拉*/
    g_ui_ctx.manual_disease_dd = lv_dropdown_create(g_ui_ctx.manual_panel);
    lv_dropdown_set_options(g_ui_ctx.manual_disease_dd, "\346\227\240\n\346\236\257\350\220\216\347\227\205\n\351\224\210\347\227\205\n\350\231\253\345\256\263\n\351\234\211\347\227\205"); /* 无
枯萎病
锈病
虫害
霉病 */
    lv_obj_set_size(g_ui_ctx.manual_disease_dd, 120, 30);
    lv_obj_set_pos(g_ui_ctx.manual_disease_dd, 328, 24);
    lv_obj_set_style_pad_top(g_ui_ctx.manual_disease_dd, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(g_ui_ctx.manual_disease_dd, 8, LV_PART_MAIN);
    /* 创建“应用按药把手动择写回测结*/
    g_ui_ctx.button_manual_apply = ui_create_action_button(g_ui_ctx.manual_panel, "\345\272\224\347\224\250", 466, 24); /* 应用 */
    /* 创建手动地块编号数字键盘容器 */
    g_ui_ctx.manual_keyboard = lv_obj_create(g_ui_ctx.monitor_page);
    lv_obj_set_size(g_ui_ctx.manual_keyboard, 320, 180);
    lv_obj_set_pos(g_ui_ctx.manual_keyboard, 300, 150);
    lv_obj_clear_flag(g_ui_ctx.manual_keyboard, LV_OBJ_FLAG_SCROLLABLE);
    /* 创建数字键盘标标题 */
    panel_label = lv_label_create(g_ui_ctx.manual_keyboard);
    lv_label_set_text(panel_label, "\345\234\260\345\235\227\347\274\226\345\217\267\351\224\256\347\233\230"); /* 地块编号键盘 */
    ui_apply_section_font(panel_label);
    lv_obj_set_pos(panel_label, 8, 4);
    /* 创建数字键盘按键 1 */
    g_ui_ctx.manual_keypad[0] = ui_create_keypad_button(g_ui_ctx.manual_keyboard, "1", 8, 28);
    /* 创建数字键盘按键 2 */
    g_ui_ctx.manual_keypad[1] = ui_create_keypad_button(g_ui_ctx.manual_keyboard, "2", 76, 28);
    /* 创建数字键盘按键 3 */
    g_ui_ctx.manual_keypad[2] = ui_create_keypad_button(g_ui_ctx.manual_keyboard, "3", 144, 28);
    /* 创建数字键盘按键 4 */
    g_ui_ctx.manual_keypad[3] = ui_create_keypad_button(g_ui_ctx.manual_keyboard, "4", 8, 72);
    /* 创建数字键盘按键 5 */
    g_ui_ctx.manual_keypad[4] = ui_create_keypad_button(g_ui_ctx.manual_keyboard, "5", 76, 72);
    /* 创建数字键盘按键 6 */
    g_ui_ctx.manual_keypad[5] = ui_create_keypad_button(g_ui_ctx.manual_keyboard, "6", 144, 72);
    /* 创建数字键盘按键 7 */
    g_ui_ctx.manual_keypad[6] = ui_create_keypad_button(g_ui_ctx.manual_keyboard, "7", 8, 116);
    /* 创建数字键盘按键 8 */
    g_ui_ctx.manual_keypad[7] = ui_create_keypad_button(g_ui_ctx.manual_keyboard, "8", 76, 116);
    /* 创建数字键盘按键 9 */
    g_ui_ctx.manual_keypad[8] = ui_create_keypad_button(g_ui_ctx.manual_keyboard, "9", 144, 116);
    /* 创建数字键盘按键 0 */
    g_ui_ctx.manual_keypad[9] = ui_create_keypad_button(g_ui_ctx.manual_keyboard, "0", 212, 28);
    /* 创建“删除按药用来删掉地块编号后一*/
    g_ui_ctx.manual_keypad[10] = ui_create_keypad_button(g_ui_ctx.manual_keyboard, "\345\210\240\351\231\244", 212, 72); /* 删除 */
    /* 创建“确定按药用来收起数字键盘 */
    g_ui_ctx.manual_keypad[11] = ui_create_keypad_button(g_ui_ctx.manual_keyboard, "\347\241\256\345\256\232", 212, 116); /* 确定 */
    lv_obj_add_event_cb(g_ui_ctx.manual_keyboard, ui_manual_keyboard_event_cb, LV_EVENT_ALL, 0);
    lv_obj_add_flag(g_ui_ctx.manual_keyboard, LV_OBJ_FLAG_HIDDEN);
    /* 创建监测页信息面板，放摘要 and 地块详情 */
    g_ui_ctx.info_panel = lv_obj_create(g_ui_ctx.monitor_page);
    lv_obj_set_size(g_ui_ctx.info_panel, info_w, grid_h);
    lv_obj_set_pos(g_ui_ctx.info_panel, 12, content_y);
    lv_obj_clear_flag(g_ui_ctx.info_panel, LV_OBJ_FLAG_SCROLLABLE);
    /* 创建监测摘键盘标标题 */
    panel_label = lv_label_create(g_ui_ctx.info_panel);
    lv_label_set_text(panel_label, "\347\233\221\346\265\213\346\221\230\350\246\201"); /* 监测摘要 */
    ui_apply_section_font(panel_label);
    lv_obj_set_pos(panel_label, 8, 4);
    /* 创建监测摘键盘内容标标签 */
    g_ui_ctx.summary_label = lv_label_create(g_ui_ctx.info_panel);
    lv_label_set_long_mode(g_ui_ctx.summary_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_ui_ctx.summary_label, info_w - 24);
    lv_obj_set_pos(g_ui_ctx.summary_label, 8, 28);
    lv_label_set_text(g_ui_ctx.summary_label, "\346\227\240\347\233\221\346\265\213\346\225\260\346\215\256"); /* 无监测数据 */
    /* 创建地块详情标标题 */
    panel_label = lv_label_create(g_ui_ctx.info_panel);
    lv_label_set_text(panel_label, "\345\234\260\345\235\227\350\257\246\346\203\205"); /* 地块详情 */
    ui_apply_section_font(panel_label);
    lv_obj_set_pos(panel_label, 8, 230);
    /* 创建地块详情内容标标签 */
    g_ui_ctx.detail_label = lv_label_create(g_ui_ctx.info_panel);
    lv_label_set_long_mode(g_ui_ctx.detail_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_ui_ctx.detail_label, info_w - 24);
    lv_obj_set_pos(g_ui_ctx.detail_label, 8, 250);
    lv_label_set_text(g_ui_ctx.detail_label, "\345\234\260\345\235\227\350\257\246\346\203\205\357\274\232\346\227\240"); /* 地块详情：无 */
    /* 创建监测页地块网格容*/
    g_ui_ctx.grid_panel = lv_obj_create(g_ui_ctx.monitor_page);
    lv_obj_set_size(g_ui_ctx.grid_panel, grid_w, grid_h);
    lv_obj_set_pos(g_ui_ctx.grid_panel, grid_x, grid_y);
    lv_obj_clear_flag(g_ui_ctx.grid_panel, LV_OBJ_FLAG_SCROLLABLE);
    /* 创建监测页图例面*/
    legend_panel = lv_obj_create(g_ui_ctx.monitor_page);
    lv_obj_set_size(legend_panel, legend_w, grid_h);
    lv_obj_set_pos(legend_panel, (lv_coord_t)(grid_x + grid_w + 12), grid_y);
    lv_obj_clear_flag(legend_panel, LV_OBJ_FLAG_SCROLLABLE);
    /* 创建地块图例标标题 */
    panel_label = lv_label_create(legend_panel);
    lv_label_set_text(panel_label, "\345\234\260\345\235\227\345\233\276\344\276\213"); /* 地块图例 */
    ui_apply_section_font(panel_label);
    lv_obj_set_pos(panel_label, 8, 4);
    /* 创建“健康地块图例项 */
    ui_create_legend_item(legend_panel, "\345\201\245\345\272\267", lv_palette_main(LV_PALETTE_GREEN), 10, 36); /* 健康 */
    /* 创建“缺水地块图例项 */
    ui_create_legend_item(legend_panel, "\347\274\272\346\260\264", lv_palette_main(LV_PALETTE_BLUE), 10, 66); /* 缺水 */
    /* 创建“枯萎病”地块图例项 */
    ui_create_legend_item(legend_panel, "\346\236\257\350\220\216\347\227\205", lv_palette_main(LV_PALETTE_RED), 10, 96); /* 枯萎病 */
    /* 创建“锈病地块图例项 */
    ui_create_legend_item(legend_panel, "\351\224\210\347\227\205", lv_palette_main(LV_PALETTE_ORANGE), 10, 126); /* 锈病 */
    /* 创建“虫害地块图例项 */
    ui_create_legend_item(legend_panel, "\350\231\253\345\256\263", lv_palette_main(LV_PALETTE_AMBER), 10, 156); /* 虫害 */
    /* 创建“霉病地块图例项 */
    ui_create_legend_item(legend_panel, "\351\234\211\347\227\205", lv_palette_main(LV_PALETTE_PURPLE), 10, 186); /* 霉病 */
    /* 创建配药页左侧摘要面*/
    mix_panel_left = lv_obj_create(g_ui_ctx.mix_page);
    lv_obj_set_size(mix_panel_left, 320, 500);
    lv_obj_set_pos(mix_panel_left, 12, 12);
    lv_obj_set_scroll_dir(mix_panel_left, LV_DIR_VER);
    /* 创建配药摘键盘标标题 */
    panel_label = lv_label_create(mix_panel_left);
    lv_label_set_text(panel_label, "\351\205\215\350\215\257\346\221\230\350\246\201"); /* 配药摘要 */
    ui_apply_section_font(panel_label);
    lv_obj_set_pos(panel_label, 8, 4);
    /* 创建配药状标*/
    g_ui_ctx.mix_state_label = lv_label_create(mix_panel_left);
    lv_obj_set_width(g_ui_ctx.mix_state_label, 292);
    lv_obj_set_pos(g_ui_ctx.mix_state_label, 8, 28);
    lv_label_set_text(g_ui_ctx.mix_state_label, "\351\205\215\350\215\257\347\212\266\346\200\201\357\274\232\347\251\272\351\227\262"); /* 配药状态：空闲 */
    /* 创建药箱容量字段说明标签 */
    panel_label = lv_label_create(mix_panel_left);
    lv_label_set_text(panel_label, "\345\215\225\346\254\241\350\243\205\350\275\275\345\256\271\351\207\217"); /* 单次装载容量 */
    lv_obj_set_pos(panel_label, 8, 58);
    /* 创建药箱容量下拉*/
    g_ui_ctx.mix_capacity_dd = lv_dropdown_create(mix_panel_left);
    ui_mix_build_capacity_options();
    lv_dropdown_set_options(g_ui_ctx.mix_capacity_dd, g_ui_ctx.mix_capacity_options);
    lv_dropdown_set_selected(g_ui_ctx.mix_capacity_dd,
                             ui_mix_find_capacity_index(g_data_default_drone.tank_capacity_ml_x10));
    lv_obj_set_size(g_ui_ctx.mix_capacity_dd, 120, 36);
    lv_obj_set_pos(g_ui_ctx.mix_capacity_dd, 160, 52);
    lv_obj_set_style_text_align(g_ui_ctx.mix_capacity_dd, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    /* 创建“运行配起飞按*/
    g_ui_ctx.button_mix_run = lv_btn_create(mix_panel_left);
    lv_obj_set_size(g_ui_ctx.button_mix_run, 120, 30);
    lv_obj_set_pos(g_ui_ctx.button_mix_run, 8, 94);
    lv_obj_add_event_cb(g_ui_ctx.button_mix_run, ui_button_event_cb, LV_EVENT_CLICKED, 0);
    /* 创建“运行配起飞按终*/
    panel_label = lv_label_create(g_ui_ctx.button_mix_run);
    lv_label_set_text(panel_label, "\350\277\220\350\241\214\351\205\215\350\215\257"); /* 运行配药 */
    lv_obj_center(panel_label);
    /* 创建配药警告标标签 */
    g_ui_ctx.mix_warning_label = lv_label_create(mix_panel_left);
    lv_label_set_long_mode(g_ui_ctx.mix_warning_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_ui_ctx.mix_warning_label, 292);
    lv_obj_set_pos(g_ui_ctx.mix_warning_label, 8, 266);
    lv_label_set_text(g_ui_ctx.mix_warning_label, "\350\255\246\345\221\212\357\274\232\346\227\240"); /* 警告：无 */
    /* 创建配药指标卡片容器 */
    g_ui_ctx.mix_metric_panel = lv_obj_create(mix_panel_left);
    lv_obj_set_size(g_ui_ctx.mix_metric_panel, 292, 220);
    lv_obj_set_pos(g_ui_ctx.mix_metric_panel, 8, 138);
    lv_obj_clear_flag(g_ui_ctx.mix_metric_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(g_ui_ctx.mix_metric_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(g_ui_ctx.mix_metric_panel, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_ui_ctx.mix_metric_panel, lv_color_hex(0xF4F7F2), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_ui_ctx.mix_metric_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_pos(g_ui_ctx.mix_warning_label, 8, 370);
    lv_label_set_text(g_ui_ctx.mix_warning_label, "");
    /* 创建配药页右侧批次和桶任务面*/
    mix_panel_right = lv_obj_create(g_ui_ctx.mix_page);
    lv_obj_set_size(mix_panel_right, 668, 500);
    lv_obj_set_pos(mix_panel_right, 344, 12);
    lv_obj_set_scroll_dir(mix_panel_right, LV_DIR_VER);
    /* 创建配药批次标标题 */
    panel_label = lv_label_create(mix_panel_right);
    lv_label_set_text(panel_label, "\351\205\215\350\215\257\346\211\271\346\254\241"); /* 配药批次 */
    ui_apply_section_font(panel_label);
    lv_obj_set_pos(panel_label, 8, 4);
    /* 创建主批次卡片容*/
    g_ui_ctx.mix_main_visual_panel = lv_obj_create(mix_panel_right);
    lv_obj_set_size(g_ui_ctx.mix_main_visual_panel, 644, 176);
    lv_obj_set_pos(g_ui_ctx.mix_main_visual_panel, 8, 28);
    lv_obj_set_scroll_dir(g_ui_ctx.mix_main_visual_panel, LV_DIR_VER);
    lv_obj_set_style_radius(g_ui_ctx.mix_main_visual_panel, 10, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_ui_ctx.mix_main_visual_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_ui_ctx.mix_main_visual_panel, lv_color_hex(0xF6F8F4), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_ui_ctx.mix_main_visual_panel, LV_OPA_COVER, LV_PART_MAIN);
    /* 创建桶任务时间线标标题 */
    panel_label = lv_label_create(mix_panel_right);
    lv_label_set_text(panel_label, "\346\241\266\344\273\273\345\212\241\346\227\266\351\227\264\347\272\277"); /* 桶任务时间线 */
    ui_apply_section_font(panel_label);
    lv_obj_set_pos(panel_label, 8, 214);
    /* 创建桶任务时间线容器 */
    g_ui_ctx.mix_bucket_visual_panel = lv_obj_create(mix_panel_right);
    lv_obj_set_size(g_ui_ctx.mix_bucket_visual_panel, 644, 260);
    lv_obj_set_pos(g_ui_ctx.mix_bucket_visual_panel, 8, 240);
    lv_obj_set_scroll_dir(g_ui_ctx.mix_bucket_visual_panel, LV_DIR_VER);
    lv_obj_set_style_radius(g_ui_ctx.mix_bucket_visual_panel, 10, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_ui_ctx.mix_bucket_visual_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_ui_ctx.mix_bucket_visual_panel, lv_color_hex(0xF6F8F4), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_ui_ctx.mix_bucket_visual_panel, LV_OPA_COVER, LV_PART_MAIN);
    /* 创建规划页顶部状态面*/
    plan_header_panel = lv_obj_create(g_ui_ctx.plan_page);
    g_ui_ctx.plan_header_panel = plan_header_panel;
    lv_obj_set_size(plan_header_panel, 912, 38);
    lv_obj_set_pos(plan_header_panel, 4, 0);
    lv_obj_clear_flag(plan_header_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(plan_header_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(plan_header_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(plan_header_panel, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(plan_header_panel, 0, LV_PART_MAIN);
    /* 创建规划页标*/
    panel_label = lv_label_create(plan_header_panel);
    lv_label_set_text(panel_label, "\350\247\204\345\210\222"); /* 规划 */
    ui_apply_section_font(panel_label);
    lv_obj_set_pos(panel_label, 8, 5);
    /* 创建规划状标*/
    g_ui_ctx.plan_state_label = lv_label_create(plan_header_panel);
    lv_label_set_long_mode(g_ui_ctx.plan_state_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_ui_ctx.plan_state_label, 560);
    lv_obj_set_pos(g_ui_ctx.plan_state_label, 78, 8);
    lv_label_set_text(g_ui_ctx.plan_state_label, "\350\247\204\345\210\222\347\212\266\346\200\201\357\274\232\347\251\272\351\227\262"); /* 规划状态：空闲 */
    /* 创建规划地图/指标区域容器 */
    g_ui_ctx.plan_metric_panel = lv_obj_create(g_ui_ctx.plan_page);
    lv_obj_set_size(g_ui_ctx.plan_metric_panel, 580, 330);
    lv_obj_set_pos(g_ui_ctx.plan_metric_panel, 12, 46);
    lv_obj_clear_flag(g_ui_ctx.plan_metric_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(g_ui_ctx.plan_metric_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(g_ui_ctx.plan_metric_panel, 14, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_ui_ctx.plan_metric_panel, lv_color_hex(0xF6F8F4), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_ui_ctx.plan_metric_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_ui_ctx.plan_metric_panel, 0, LV_PART_MAIN);
    /* 创建规划页下方路径任务面*/
    plan_panel_mid = lv_obj_create(g_ui_ctx.plan_page);
    lv_obj_set_size(plan_panel_mid, 912, 144);
    lv_obj_set_pos(plan_panel_mid, 12, 384);
    lv_obj_clear_flag(plan_panel_mid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(plan_panel_mid, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(plan_panel_mid, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(plan_panel_mid, 0, LV_PART_MAIN);
    /* 创建累加任务标标题 */
    panel_label = lv_label_create(plan_panel_mid);
    lv_label_set_text(panel_label, "\350\267\257\345\276\204\344\273\273\345\212\241"); /* 路径任务 */
    ui_apply_section_font(panel_label);
    lv_obj_set_pos(panel_label, 8, 4);
    /* 创建编号向滚动的累加任务列表容器 */
    g_ui_ctx.plan_timeline_panel = lv_obj_create(plan_panel_mid);
    lv_obj_set_size(g_ui_ctx.plan_timeline_panel, 896, 108);
    lv_obj_set_pos(g_ui_ctx.plan_timeline_panel, 8, 28);
    lv_obj_set_scroll_dir(g_ui_ctx.plan_timeline_panel, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(g_ui_ctx.plan_timeline_panel, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_radius(g_ui_ctx.plan_timeline_panel, 14, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_ui_ctx.plan_timeline_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_ui_ctx.plan_timeline_panel, lv_color_hex(0xF6F8F4), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_ui_ctx.plan_timeline_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_ui_ctx.plan_timeline_panel, 0, LV_PART_MAIN);
    /* 创建规划页右侧路径状态情面*/
    plan_panel_right = lv_obj_create(g_ui_ctx.plan_page);
    lv_obj_set_size(plan_panel_right, 332, 330);
    lv_obj_set_pos(plan_panel_right, 604, 46);
    lv_obj_clear_flag(plan_panel_right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(plan_panel_right, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(plan_panel_right, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(plan_panel_right, 0, LV_PART_MAIN);
    /* 创建累加详情标标题标标签 */
    g_ui_ctx.plan_detail_title_label = lv_label_create(plan_panel_right);
    lv_label_set_text(g_ui_ctx.plan_detail_title_label, "\345\267\262\351\200\211\350\267\257\345\276\204"); /* 已选路径 */
    ui_apply_section_font(g_ui_ctx.plan_detail_title_label);
    lv_obj_set_pos(g_ui_ctx.plan_detail_title_label, 8, 8);
    /* 创建“运行规划”按钮 */
    g_ui_ctx.button_plan_run = lv_btn_create(plan_panel_right);
    lv_obj_set_size(g_ui_ctx.button_plan_run, 120, 30);
    lv_obj_set_pos(g_ui_ctx.button_plan_run, 204, 2);
    lv_obj_add_event_cb(g_ui_ctx.button_plan_run, ui_button_event_cb, LV_EVENT_CLICKED, 0);
    panel_label = lv_label_create(g_ui_ctx.button_plan_run);
    lv_label_set_text(panel_label, "\350\277\220\350\241\214\350\247\204\345\210\222"); /* 运行规划 */
    lv_obj_center(panel_label);
    /* 创建累加详情内容容器 */
    g_ui_ctx.plan_detail_panel = lv_obj_create(plan_panel_right);
    lv_obj_set_size(g_ui_ctx.plan_detail_panel, 316, 286);
    lv_obj_set_pos(g_ui_ctx.plan_detail_panel, 8, 38);
    lv_obj_set_scroll_dir(g_ui_ctx.plan_detail_panel, LV_DIR_VER);
    lv_obj_set_style_radius(g_ui_ctx.plan_detail_panel, 14, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_ui_ctx.plan_detail_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_ui_ctx.plan_detail_panel, lv_color_hex(0xF6F8F4), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_ui_ctx.plan_detail_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_ui_ctx.plan_detail_panel, 0, LV_PART_MAIN);
    lv_scr_load(g_ui_ctx.screen);
    /* 初次进入控制新默认状态，不自动跑业务流程 */
    ui_monitor_update_view(APP_OK);
    ui_mix_update_view(APP_ERR_STATE);
    ui_plan_update_view(APP_ERR_STATE);
    ui_sim_update_view(APP_ERR_STATE);
    ui_switch_page(UI_PAGE_MONITOR);
}

/* 主循环UI轻量刷新入口 */
void ui_module_refresh(void)
{
    static app_state_t last_state = APP_STATE_IDLE;
    static uint32_t last_map_elapsed_ms = 0xFFFFFFFFUL;
    static uint32_t last_text_elapsed_ms = 0xFFFFFFFFUL;
    static app_point_t last_map_position = {0, 0};
    static app_point_t last_text_position = {0, 0};
    static sim_state_t last_map_sim_state = SIM_STATE_IDLE;
    static sim_state_t last_text_sim_state = SIM_STATE_IDLE;
    const sim_output_t *sim_output;
    app_state_t state;
    /* 这里不创建大对象，页面重建交给服务控制按钮事件 */
    if (g_ui_ctx.screen == 0) {
        return;
    }
    state = app_core_get_state();
    sim_output = app_core_get_sim_output();
    if (state != last_state) {
        last_map_elapsed_ms = 0xFFFFFFFFUL;
        last_text_elapsed_ms = 0xFFFFFFFFUL;
        last_map_sim_state = SIM_STATE_IDLE;
        last_text_sim_state = SIM_STATE_IDLE;
        last_map_position.x_mm = 0;
        last_map_position.y_mm = 0;
        last_text_position.x_mm = 0;
        last_text_position.y_mm = 0;
    }
    /*
     * 浠跨湡杩愯鏃舵媶鍒嗗埛鏂伴鐜囷細
     * 椋炴満浣嶇疆璺熼殢 tick锛屾枃瀛椾綆棰戝埛鏂?
     */
    if (state == APP_STATE_SIM_RUNNING) {
        if ((sim_output->elapsed_time_ms != last_map_elapsed_ms) ||
            (sim_output->state != last_map_sim_state) ||
            (sim_output->current_position.x_mm != last_map_position.x_mm) ||
            (sim_output->current_position.y_mm != last_map_position.y_mm)) {
            ui_sim_update_map();
            last_map_elapsed_ms = sim_output->elapsed_time_ms;
            last_map_sim_state = sim_output->state;
            last_map_position = sim_output->current_position;
        }
    } else if (g_ui_ctx.sim_grid_plane_icon != 0) {
        lv_obj_add_flag(g_ui_ctx.sim_grid_plane_icon, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_ui_ctx.current_page == UI_PAGE_SIM) {
        if (state == APP_STATE_SIM_RUNNING) {
            if ((sim_output->state != last_text_sim_state) ||
                (sim_output->current_position.x_mm != last_text_position.x_mm) ||
                (sim_output->current_position.y_mm != last_text_position.y_mm) ||
                (ui_elapsed_reached(sim_output->elapsed_time_ms, last_text_elapsed_ms, UI_SIM_TEXT_REFRESH_MS) != 0U)) {
                ui_sim_update_view(ui_sim_current_view_result());
                last_text_elapsed_ms = sim_output->elapsed_time_ms;
                last_text_sim_state = sim_output->state;
                last_text_position = sim_output->current_position;
            }
        } else if (last_text_elapsed_ms == 0xFFFFFFFFUL) {
            ui_sim_update_view(ui_sim_current_view_result());
            last_text_elapsed_ms = sim_output->elapsed_time_ms;
            last_text_sim_state = sim_output->state;
            last_text_position = sim_output->current_position;
        }
    }
    last_state = state;
}

/* ---- Page implementations ---- */

/* ---- Monitor page ---- */
static void ui_monitor_update_grid(void);

/* 刷新监测网格的中边枚举 */
static void ui_monitor_update_selection_style(void)
{
    uint16_t i;
    for (i = 0U; i < g_ui_ctx.grid_cell_count; i++) {
        if (g_ui_ctx.grid_cell[i] == 0) {
            continue;
        }
        if (i == g_ui_ctx.selected_plot_index) {
            lv_obj_set_style_border_width(g_ui_ctx.grid_cell[i], 3, LV_PART_MAIN);
            lv_obj_set_style_border_color(g_ui_ctx.grid_cell[i], lv_color_black(), LV_PART_MAIN);
        } else {
            lv_obj_set_style_border_width(g_ui_ctx.grid_cell[i], 1, LV_PART_MAIN);
            lv_obj_set_style_border_color(g_ui_ctx.grid_cell[i], lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_MAIN);
        }
    }
}

/* 刷新当前地块详情 */
static void ui_monitor_update_detail(uint16_t plot_index)
{
    const monitor_output_t *output;
    const plot_status_t *plot;
    char detail_text[256];
    char severity_buf[64] = "";
    output = app_core_get_monitor_output();
    if (plot_index >= output->plot_count) {
        lv_label_set_text(g_ui_ctx.detail_label, "\345\234\260\345\235\227\350\257\246\346\203\205: \346\227\240"); /* 地块详情: 无 */
        return;
    }
    plot = &output->plot_status[plot_index];
    if (plot->state == PLOT_STATE_WATER_DEFICIT) {
        const monitor_region_t *reg = ui_find_monitor_region(output, SPRAY_CONTENT_WATER, DISEASE_TYPE_NONE);
        if (reg != 0) {
            (void)snprintf(severity_buf,
                           sizeof(severity_buf),
                           " (%s %u%%)", /*  (%s %u%%) */
                           ui_severity_name(reg->severity_x100),
                           (unsigned int)reg->severity_x100);
        }
    } else if (plot->state == PLOT_STATE_DISEASE) {
        const monitor_region_t *reg = ui_find_monitor_region(output, SPRAY_CONTENT_PESTICIDE, plot->disease_type);
        if (reg != 0) {
            (void)snprintf(severity_buf,
                           sizeof(severity_buf),
                           " (%s %u%%)", /*  (%s %u%%) */
                           ui_severity_name(reg->severity_x100),
                           (unsigned int)reg->severity_x100);
        }
    }
    (void)snprintf(detail_text,
                   sizeof(detail_text),
                   "\345\234\260\345\235\227\347\274\226\345\217\267%u | \347\254\254%u\350\241\214 \347\254\254%u\345\210\227 | %s | %s%s\n" /* 地块编号%u | 第%u行 第%u列 | %s | %s%s */
                   "\351\235\242\347\247\257: %lu.%02lu\345\271\263\346\226\271\347\261\263", /* 面积: %lu.%02lu平方米 */
                   plot->plot_id,
                   (unsigned int)(plot->row + 1U),
                   (unsigned int)(plot->col + 1U),
                   ui_plot_state_name(plot->state),
                   ui_disease_name(plot->disease_type),
                   severity_buf,
                   (unsigned long)(plot->area_m2_x100 / 100U),
                   (unsigned long)(plot->area_m2_x100 % 100U));
    lv_label_set_text(g_ui_ctx.detail_label, detail_text);
}

/* 释放监测页地块按*/
void ui_monitor_release_grid_cells(void)
{
    uint16_t i;
    for (i = 0U; i < g_ui_ctx.grid_cell_count; i++) {
        if (g_ui_ctx.grid_cell[i] != 0) {
            lv_obj_del(g_ui_ctx.grid_cell[i]);
            g_ui_ctx.grid_cell[i] = 0;
        }
    }
    g_ui_ctx.grid_cell_count = 0U;
}

/* 切回监测页时重建地块网格 */
void ui_monitor_reload_grid(void)
{
    const monitor_output_t *monitor_output;
    monitor_output = app_core_get_monitor_output();
    if (monitor_output->plot_count == 0U) {
        return;
    }
    if (g_ui_ctx.selected_plot_index >= monitor_output->plot_count) {
        g_ui_ctx.selected_plot_index = 0U;
    }
    ui_monitor_update_grid();
    ui_monitor_update_detail(g_ui_ctx.selected_plot_index);
}

/* 地块按钮数量与监测测输出一*/
static void ui_monitor_ensure_grid_cells(uint16_t plot_count)
{
    uint16_t i;
    if (plot_count == g_ui_ctx.grid_cell_count) {
        return;
    }
    ui_monitor_release_grid_cells();
    for (i = 0U; i < plot_count; i++) {
        /* 创建监测网格进度控制块按*/
        g_ui_ctx.grid_cell[i] = lv_btn_create(g_ui_ctx.grid_panel);
        lv_obj_set_style_radius(g_ui_ctx.grid_cell[i], 1, LV_PART_MAIN);
        lv_obj_set_style_border_width(g_ui_ctx.grid_cell[i], 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(g_ui_ctx.grid_cell[i], lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_MAIN);
        lv_obj_add_event_cb(g_ui_ctx.grid_cell[i], ui_grid_cell_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }
    g_ui_ctx.grid_cell_count = plot_count;
}

/* 处理监测页地块点*/
void ui_grid_cell_event_cb(lv_event_t *e)
{
    uint32_t plot_index;
    char plot_id_text[8];
    plot_index = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    g_ui_ctx.selected_plot_index = (uint16_t)plot_index;
    if (g_ui_ctx.manual_plot_id_ta != 0) {
        (void)snprintf(plot_id_text, sizeof(plot_id_text), "%lu", (unsigned long)(plot_index + 1U));
        lv_textarea_set_text(g_ui_ctx.manual_plot_id_ta, plot_id_text);
    }
    ui_monitor_update_detail((uint16_t)plot_index);
    ui_monitor_update_selection_style();
}

/* 按监测测输出刷新地块颜色和布局 */
static void ui_monitor_update_grid(void)
{
    const monitor_output_t *output;
    uint32_t rows;
    uint32_t cols;
    uint32_t plot_count;
    uint32_t i;
    lv_coord_t cell_w;
    lv_coord_t cell_h;
    lv_coord_t panel_w;
    lv_coord_t panel_h;
    lv_coord_t cell_size;
    lv_coord_t draw_w;
    lv_coord_t draw_h;
    lv_coord_t offset_x;
    lv_coord_t offset_y;
    /* 测页航点地块，不叠加航点和普通*/
    output = app_core_get_monitor_output();
    rows = output->grid.rows;
    cols = output->grid.cols;
    plot_count = output->plot_count;
    if ((rows == 0U) || (cols == 0U)) {
        return;
    }
    ui_monitor_ensure_grid_cells((uint16_t)plot_count);
    /* 依据面板大小计算居中的操作方形网格 */
    panel_w = lv_obj_get_content_width(g_ui_ctx.grid_panel);
    panel_h = lv_obj_get_content_height(g_ui_ctx.grid_panel);
    cell_w = (lv_coord_t)(panel_w / cols);
    cell_h = (lv_coord_t)(panel_h / rows);
    cell_size = (cell_w < cell_h) ? cell_w : cell_h;
    if (cell_size < 10) {
        cell_size = 10;
    }
    draw_w = (lv_coord_t)(cols * (uint32_t)cell_size);
    draw_h = (lv_coord_t)(rows * (uint32_t)cell_size);
    offset_x = (lv_coord_t)((panel_w - draw_w) / 2);
    offset_y = (lv_coord_t)((panel_h - draw_h) / 2);
    if (offset_x < 0) {
        offset_x = 0;
    }
    if (offset_y < 0) {
        offset_y = 0;
    }
    /* 同步每个地块控制控件的位置与颜色 */
    for (i = 0U; i < plot_count; i++) {
        lv_obj_set_size(g_ui_ctx.grid_cell[i], cell_size - 2, cell_size - 2);
        lv_obj_set_pos(g_ui_ctx.grid_cell[i],
                       (lv_coord_t)(offset_x + ((i % cols) * cell_size)),
                       (lv_coord_t)(offset_y + ((i / cols) * cell_size)));
        lv_obj_set_style_bg_color(g_ui_ctx.grid_cell[i],
                                  ui_plot_color(&output->plot_status[i]),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_opa(g_ui_ctx.grid_cell[i], LV_OPA_COVER, LV_PART_MAIN);
    }
    ui_monitor_update_selection_style();
}

/* 准具备默监测监测输入 */
void ui_monitor_set_default_input(void)
{
    monitor_module_fill_default_input(&g_ui_ctx.monitor_input);
    ui_monitor_apply_crop_selection();
}

/* 刷新监测摘键盘、地块网格和详情 */
void ui_monitor_update_view(app_result_t result)
{
    const monitor_output_t *output;
    char summary_text[640];
    char state_text[160];
    uint32_t total_area_m2_x100;
    uint16_t disease_total;
    const char *environment_text;
    /* 监测页只monitor_output */
    output = app_core_get_monitor_output();
    /* 组输入摘键盘文字，错编号输入留提*/
    if (result == APP_OK) {
        disease_total = (uint16_t)(output->stats.disease_plot_count[0] +
                                   output->stats.disease_plot_count[1] +
                                   output->stats.disease_plot_count[2] +
                                   output->stats.disease_plot_count[3]);
        environment_text = (output->environment_status.suitable != 0U) ? "\351\200\202\345\256\234" : "\351\242\204\350\255\246"; /* 适宜，预警 */
        (void)snprintf(state_text,
                       sizeof(state_text),
                       "\347\233\221\346\265\213\345\256\214\346\210\220 | %s-%s | \347\216\257\345\242\203:%s | \345\201\245\345\272\267:%u \347\274\272\346\260\264:%u \347\227\205\345\256\263:%u | \345\214\272\345\237\237:%u", /* 监测完成 | %s-%s | 环境:%s | 健康:%u 缺水:%u 病害:%u | 区域:%u */
                       ui_crop_name(output->crop_type),
                       ui_growth_stage_name(output->growth_stage),
                       environment_text,
                       output->stats.healthy_plot_count,
                       output->stats.water_deficit_plot_count,
                       disease_total,
                       output->region_count);
    } else {
        (void)snprintf(state_text,
                       sizeof(state_text),
                       "\347\233\221\346\265\213\347\212\266\346\200\201: \346\234\252\345\260\261\347\273\252 | \350\257\267\346\243\200\346\237\245\347\275\221\346\240\274 | \347\240\201:%d", /* 监测状态: 未就绪 | 请检查网格 | 码:%d */
                       (int)result);
    }
    lv_label_set_text(g_ui_ctx.state_label, state_text);
    if (result == APP_OK) {
        total_area_m2_x100 = 0U;
        if (output->plot_count > 0U) {
            total_area_m2_x100 = output->field.area_m2_x100;
        }
        (void)snprintf(summary_text,
                       sizeof(summary_text),
                       "\344\275\234\347\211\251: %s | \347\224\237\351\225\277\346\234\237: %s\n" /* 作物: %s | 生长期: %s */
                       "\347\216\257\345\242\203: %s | \344\277\256\346\255\243: %u%%\n" /* 环境: %s | 修正: %u%% */
                       "\346\270\251\345\272\246: %u.%uC  \346\271\277\345\272\246: %u.%u%%\n" /* 温度: %u.%uC  湿度: %u.%u%% */
                       "\345\205\211\347\205\247: %lu lux  \351\243\216\351\200\237: %u.%um/s\n" /* 光照: %lu lux  风速: %u.%um/s */
                       "\347\275\221\346\240\274: %u x %u | \345\234\260\345\235\227\346\225\260: %u\n" /* 网格: %u x %u | 地块数: %u */
                       "\345\215\225\346\240\274: %u.%03u x %u.%03u\347\261\263 | \346\200\273\351\235\242\347\247\257: %lu.%02lu\345\271\263\346\226\271\347\261\263\n" /* 单格: %u.%03u x %u.%03u米 | 总面积: %lu.%02lu平方米 */
                       "\345\201\245\345\272\267: %u  \347\274\272\346\260\264: %u  \347\227\205\345\256\263: %u\n" /* 健康: %u  缺水: %u  病害: %u */
                       "\347\227\205\345\256\263\346\230\216\347\273\206: %u / %u / %u / %u\n" /* 病害明细: %u / %u / %u / %u */
                       "\345\226\267\346\264\222\351\235\242\347\247\257: %lu.%02lu\345\271\263\346\226\271\347\261\263 | \345\214\272\345\237\237: %u\n", /* 喷洒面积: %lu.%02lu平方米 | 区域: %u */
                       ui_crop_name(output->crop_type),
                       ui_growth_stage_name(output->growth_stage),
                       environment_text,
                       output->environment_status.environment_factor_x100,
                       output->environment_status.input.temperature_x10 / 10U,
                       output->environment_status.input.temperature_x10 % 10U,
                       output->environment_status.input.humidity_x10 / 10U,
                       output->environment_status.input.humidity_x10 % 10U,
                       (unsigned long)output->environment_status.input.light_lux,
                       output->environment_status.input.wind_speed_x10 / 10U,
                       output->environment_status.input.wind_speed_x10 % 10U,
                       output->grid.rows,
                       output->grid.cols,
                       output->plot_count,
                       output->grid.cell_width_mm / 1000U,
                       output->grid.cell_width_mm % 1000U,
                       output->grid.cell_height_mm / 1000U,
                       output->grid.cell_height_mm % 1000U,
                       (unsigned long)(total_area_m2_x100 / 100U),
                       (unsigned long)(total_area_m2_x100 % 100U),
                       output->stats.healthy_plot_count,
                       output->stats.water_deficit_plot_count,
                       disease_total,
                       output->stats.disease_plot_count[0],
                       output->stats.disease_plot_count[1],
                       output->stats.disease_plot_count[2],
                       output->stats.disease_plot_count[3],
                       (unsigned long)(output->stats.total_spray_area_m2_x100 / 100U),
                       (unsigned long)(output->stats.total_spray_area_m2_x100 % 100U),
                       output->region_count);
    } else {
        (void)snprintf(summary_text,
                       sizeof(summary_text),
                       "\347\275\221\346\240\274\351\205\215\347\275\256\346\227\240\346\225\210\346\210\226\347\233\221\346\265\213\346\211\247\350\241\214\345\244\261\350\264\245"); /* 网格配置无效或监测执行失败 */
    }
    lv_label_set_text(g_ui_ctx.summary_label, summary_text);
    if (result == APP_OK) {
        /* 成功时同步中项网格和详情 */
        if (output->plot_count > 0U) {
            if (g_ui_ctx.selected_plot_index >= output->plot_count) {
                g_ui_ctx.selected_plot_index = 0U;
            }
        } else {
            g_ui_ctx.selected_plot_index = 0U;
        }
        ui_monitor_update_grid();
        ui_monitor_update_detail(g_ui_ctx.selected_plot_index);
    } else {
        ui_monitor_update_detail(APP_MAX_PLOT_COUNT);
    }
}

/* 运行初监测化监测测或控制*/
static uint32_t ui_random_next(uint32_t *seed)
{
    *seed = (*seed * 1103515245UL) + 12345UL;
    return *seed;
}

void ui_monitor_run(ui_monitor_action_t action)
{
    app_result_t result;
    if (g_ui_ctx.monitor_stats_popup != 0) {
        lv_obj_del(g_ui_ctx.monitor_stats_popup);
        g_ui_ctx.monitor_stats_popup = 0;
    }
    ui_monitor_set_default_input();
    if (action == UI_MONITOR_ACTION_AUTO) {
        uint32_t rand_seed = lv_tick_get();
        if (rand_seed == 0U) {
            rand_seed = 1234567UL;
        }
        g_ui_ctx.monitor_input.mode = MONITOR_MODE_AUTO_DETECT;
        g_ui_ctx.monitor_input.random_seed = rand_seed;
        /* 随机温度 10.0C 到 40.0C (100 到 400) */
        g_ui_ctx.monitor_input.environment.temperature_x10 = (uint16_t)((ui_random_next(&rand_seed) % 301U) + 100U);
        /* 随机湿度 30.0% 到 95.0% (300 到 950) */
        g_ui_ctx.monitor_input.environment.humidity_x10 = (uint16_t)((ui_random_next(&rand_seed) % 651U) + 300U);
        /* 随机光照 5000 到 85000 lux */
        g_ui_ctx.monitor_input.environment.light_lux = (uint32_t)((ui_random_next(&rand_seed) % 80001UL) + 5000UL);
        /* 随机风速 0.0 到 8.0 m/s (0 到 80) */
        g_ui_ctx.monitor_input.environment.wind_speed_x10 = (uint16_t)(ui_random_next(&rand_seed) % 81U);
    }
    result = app_core_run_monitor(&g_ui_ctx.monitor_input);
    ui_monitor_update_view(result);
    if (result == APP_OK) {
        ui_mix_update_view(APP_ERR_STATE);
        ui_plan_update_view(APP_ERR_STATE);
        ui_sim_update_view(APP_ERR_STATE);
    }
}

/* ---- Mix page ---- */
#define UI_MIX_CARD_RADIUS 8

/* 创建配药页卡*/
static lv_obj_t *ui_mix_create_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *card;
    /* 创建配药页卡片容*/
    card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, x, y);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(card, UI_MIX_CARD_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_palette_lighten(LV_PALETTE_GREY, 2), LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(card, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    return card;
}

static const mix_recipe_t *ui_mix_find_recipe(spray_content_t spray_content, disease_type_t disease_type)
{
    uint16_t i;
    for (i = 0U; i < g_data_mix_recipe_count; i++) {
        if ((g_data_mix_recipe_table[i].spray_content == spray_content) &&
            (g_data_mix_recipe_table[i].disease_type == disease_type)) {
            return &g_data_mix_recipe_table[i];
        }
    }
    return 0;
}

static const char *ui_mix_content_name(spray_content_t spray_content, disease_type_t disease_type)
{
    if (spray_content == SPRAY_CONTENT_WATER) {
        return "\346\270\205\346\260\264"; /* 清水 */
    }
    return ui_disease_name(disease_type);
}

static void ui_add_fill_bar(lv_obj_t *parent,
                            lv_coord_t x,
                            lv_coord_t y,
                            lv_coord_t w,
                            lv_coord_t h,
                            uint16_t pct,
                            lv_color_t color)
{
    lv_obj_t *bar;
    if (pct > 100U) {
        pct = 100U;
    }
    bar = lv_bar_create(parent);
    lv_obj_set_size(bar, w, h);
    lv_obj_set_pos(bar, x, y);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, pct, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar, (lv_coord_t)(h / 2), LV_PART_MAIN);
    lv_obj_set_style_radius(bar, (lv_coord_t)(h / 2), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, lv_palette_lighten(LV_PALETTE_GREY, 2), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
}

static void ui_add_ratio_bar(lv_obj_t *parent,
                             lv_coord_t x,
                             lv_coord_t y,
                             lv_coord_t w,
                             lv_coord_t h,
                             uint16_t water_pct,
                             uint16_t pesticide_pct)
{
    lv_obj_t *bar;
    if ((water_pct + pesticide_pct) > 100U) {
        pesticide_pct = (uint16_t)(100U - water_pct);
    }
    bar = lv_bar_create(parent);
    lv_obj_set_size(bar, w, h);
    lv_obj_set_pos(bar, x, y);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, water_pct, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar, (lv_coord_t)(h / 2), LV_PART_MAIN);
    lv_obj_set_style_radius(bar, (lv_coord_t)(h / 2), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);
}

/* 添加配药指标*/
static void ui_mix_add_metric_card(lv_obj_t *parent,
                               const char *title,
                               const char *value,
                               const char *hint,
                               lv_coord_t x,
                               lv_coord_t y)
{
    lv_obj_t *card;
    /* 创建配药指标卡片 */
    card = ui_mix_create_card(parent, x, y, 118, 88);
    /* 创建指标标标题标标签 */
    ui_add_label(card, title, 10, 10, 98, 0U, lv_color_hex(0x56645B));
    /* 创建指标数标*/
    ui_add_label(card, value, 10, 34, 98, 1U, lv_color_hex(0x1F2A24));
    /* 创建指标说明标标签 */
    ui_add_label(card, hint, 10, 62, 98, 0U, lv_color_hex(0x7A877F));
}

/* 添加配药主批次卡*/
static void ui_add_main_batch_card(lv_obj_t *parent, const mix_main_batch_t *main_batch, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *card;
    char text[160];
    const char *number_name;
    const mix_recipe_t *recipe;
    const monitor_output_t *monitor_output;
    uint32_t recipe_total;
    uint16_t water_pct;
    uint16_t pesticide_pct;
    uint16_t env_factor;
    lv_color_t color;
    color = ui_content_color(main_batch->spray_content, main_batch->disease_type);
    /* 创建配药主批次卡*/
    card = ui_mix_create_card(parent, x, y, 260, 120);
    lv_obj_set_style_border_color(card, color, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 2, LV_PART_MAIN);
    recipe = ui_mix_find_recipe(main_batch->spray_content, main_batch->disease_type);
    recipe_total = 0U;
    water_pct = 100U;
    pesticide_pct = 0U;
    if (recipe != 0) {
        recipe_total = recipe->water_ml_per_m2_x100 + recipe->pesticide_ml_per_m2_x100;
        if (recipe_total > 0U) {
            water_pct = (uint16_t)((recipe->water_ml_per_m2_x100 * 100UL) / recipe_total);
            if (water_pct > 100U) {
                water_pct = 100U;
            }
            pesticide_pct = (uint16_t)(100U - water_pct);
        }
    }
    monitor_output = app_core_get_monitor_output();
    env_factor = (monitor_output != 0) ? monitor_output->environment_status.environment_factor_x100 : 100U;
    number_name = ui_cn_number_name(main_batch->main_batch_id);
    if (number_name != 0) {
        (void)snprintf(text,
                       sizeof(text),
                       "\344\270\273\351\205\215\346\226\271%s  %s  %s", /* 主配方%s  %s  %s */
                       number_name,
                       (main_batch->spray_content == SPRAY_CONTENT_WATER) ? "\346\270\205\346\260\264" : "\345\206\234\350\215\257", /* 清水，农药 */
                       ui_disease_name(main_batch->disease_type));
    } else {
        (void)snprintf(text,
                       sizeof(text),
                       "\344\270\273\351\205\215\346\226\271%u  %s  %s", /* 主配方%u  %s  %s */
                       main_batch->main_batch_id,
                       (main_batch->spray_content == SPRAY_CONTENT_WATER) ? "\346\270\205\346\260\264" : "\345\206\234\350\215\257", /* 清水，农药 */
                       ui_disease_name(main_batch->disease_type));
    }
    /* 创建主批次名称标*/
    ui_add_label(card, text, 10, 8, 240, 1U, color);
    (void)snprintf(text,
                   sizeof(text),
                   "\345\206\263\347\255\226:%u%% \347\216\257\345\242\203:%u%%", /* 决策:%u%% 环境:%u%% */
                   main_batch->severity_x100,
                   env_factor);
    ui_add_label(card, text, 10, 34, 240, 0U, lv_color_hex(0x333333));
    (void)snprintf(text,
                   sizeof(text),
                   "\346\241\266\344\273\273\345\212\241:%u", /* 桶任务:%u */
                   main_batch->bucket_count);
    /* 创建主批次桶数量标标签 */
    ui_add_label(card, text, 10, 56, 240, 0U, lv_color_hex(0x333333));
    (void)snprintf(text,
                   sizeof(text),
                   "\346\257\224\344\276\213 \346\270\205\346\260\264%u%% \345\206\234\350\215\257%u%%", /* 比例 清水%u%% 农药%u%% */
                   water_pct,
                   pesticide_pct);
    ui_add_label(card, text, 10, 78, 240, 0U, lv_color_hex(0x56645B));
    ui_add_ratio_bar(card, 10, 104, 240, 8, water_pct, pesticide_pct);
}

static const char *ui_get_sub_batch_prefix(uint16_t sub_batch_id)
{
    const mix_output_t *mix_output = app_core_get_mix_output();
    const plan_output_t *plan_output = app_core_get_plan_output();
    uint16_t i;
    if (mix_output == 0 || plan_output == 0) {
        return "";
    }
    for (i = 0U; i < plan_output->sub_batch_count; i++) {
        if (plan_output->sub_batch[i].sub_batch_id == sub_batch_id) {
            uint16_t mix_idx = plan_output->sub_batch[i].mix_sub_batch_index;
            if (mix_idx < mix_output->sub_batch_count) {
                if (mix_output->sub_batch[mix_idx].spray_content == SPRAY_CONTENT_WATER) {
                    return "\346\270\205\346\260\264"; /* 清水 */
                } else {
                    return "\345\206\234\350\215\257"; /* 农药 */
                }
            }
        }
    }
    return "";
}

/* 添加只任务*/
static void ui_add_bucket_block(lv_obj_t *parent,
                                const mix_sub_batch_t *sub_batch,
                                lv_coord_t x,
                                lv_coord_t y)
{
    lv_obj_t *card;
    char text[96];
    const char *number_name;
    lv_color_t color;
    color = ui_content_color(sub_batch->spray_content, sub_batch->disease_type);
    /* 创建配药桶任务卡*/
    card = ui_mix_create_card(parent, x, y, 125, 78);
    lv_obj_set_style_border_color(card, color, LV_PART_MAIN);
    {
        const char *prefix = (sub_batch->spray_content == SPRAY_CONTENT_WATER) ? "\346\270\205\346\260\264" : "\345\206\234\350\215\257"; /* 清水，农药 */
        number_name = ui_cn_number_name(sub_batch->sub_batch_id);
        if (number_name != 0) {
            (void)snprintf(text, sizeof(text), "%s\344\273\273\345\212\241%s", prefix, number_name); /* %s任务%s */
        } else {
            (void)snprintf(text, sizeof(text), "%s\344\273\273\345\212\241%u", prefix, sub_batch->sub_batch_id); /* %s任务%u */
        }
    }
    /* 创建桶任务编号标*/
    ui_add_label(card, text, 10, 6, 105, 1U, color);
    (void)snprintf(text,
                   sizeof(text),
                   "%lu.%01luL",
                   (unsigned long)(sub_batch->liquid_ml_x10 / 10000U),
                   (unsigned long)((sub_batch->liquid_ml_x10 % 10000U) / 1000U));
    /* 创建桶任务液量标*/
    ui_add_label(card, text, 10, 31, 105, 0U, lv_color_hex(0x333333));
    {
        uint32_t capacity_ml_x10;
        uint16_t load_pct;
        capacity_ml_x10 = ui_mix_selected_capacity_ml_x10();
        load_pct = 0U;
        if (capacity_ml_x10 > 0U) {
            load_pct = (uint16_t)((sub_batch->liquid_ml_x10 * 100UL) / capacity_ml_x10);
            if (load_pct > 100U) {
                load_pct = 100U;
            }
        }
        (void)snprintf(text,
                       sizeof(text),
                       "\350\243\205\350\275\275%u%%", /* 装载%u%% */
                       load_pct);
        ui_add_label(card, text, 10, 50, 105, 0U, lv_color_hex(0x56645B));
        ui_add_fill_bar(card, 10, 68, 105, 5, load_pct, color);
    }
}

/* 渲染配药桶任务时间线 */
static void ui_mix_render_bucket_page(const mix_output_t *mix_output)
{
    uint16_t i;
    lv_coord_t x;
    lv_coord_t y;
    char text[96];
    ui_clean_container(g_ui_ctx.mix_bucket_visual_panel);
    if ((g_ui_ctx.mix_bucket_visual_panel == 0) || (mix_output == 0)) {
        return;
    }
    if (mix_output->sub_batch_count == 0U) {
        /* 创建配药桶任务空状标*/
        ui_add_label(g_ui_ctx.mix_bucket_visual_panel, "\346\232\202\346\227\240\346\241\266\344\273\273\345\212\241", 8, 8, 220, 1U, lv_color_hex(0x333333)); /* 暂无桶任务 */
        return;
    }
    (void)snprintf(text, sizeof(text), "\346\241\266\344\273\273\345\212\241  \345\205\261%u\346\241\266", mix_output->sub_batch_count); /* 桶任务  共%u桶 */
    /* 创建桶任务列表标题标*/
    ui_add_label(g_ui_ctx.mix_bucket_visual_panel, text, 8, 10, 320, 1U, lv_color_hex(0x1F2A24));
    for (i = 0U; i < mix_output->sub_batch_count; i++) {
        x = (lv_coord_t)(8 + (i % 4U) * 146U);
        y = (lv_coord_t)(46 + (i / 4U) * 88U);
        ui_add_bucket_block(g_ui_ctx.mix_bucket_visual_panel, &mix_output->sub_batch[i], x, y);
    }
}

/* 清空配药页动态卡*/
void ui_mix_release_visual_content(void)
{
    ui_clean_container(g_ui_ctx.mix_metric_panel);
    ui_clean_container(g_ui_ctx.mix_main_visual_panel);
    ui_clean_container(g_ui_ctx.mix_bucket_visual_panel);
    if (g_ui_ctx.mix_warning_label != 0) {
        lv_label_set_text(g_ui_ctx.mix_warning_label, "");
    }
}

/* 切回配药页时按当前状态重建内*/
void ui_mix_reload_view(void)
{
    const mix_output_t *mix_output;
    app_state_t state;
    mix_output = app_core_get_mix_output();
    state = app_core_get_state();
    if ((mix_output->sub_batch_count > 0U) &&
        ((state == APP_STATE_MIX_DONE) ||
         (state == APP_STATE_PLAN_DONE) ||
         (state == APP_STATE_SIM_RUNNING) ||
         (state == APP_STATE_SIM_DONE))) {
        ui_mix_update_view(APP_OK);
    } else {
        ui_mix_update_view(APP_ERR_STATE);
    }
}

/* 刷新配药摘键盘、批次和告警 */
void ui_mix_update_view(app_result_t result)
{
    const mix_output_t *mix_output;
    char state_text[128];
    char value_text[64];
    char warning_text[160];
    uint16_t i;
    lv_coord_t x;
    lv_coord_t y;
    mix_output = app_core_get_mix_output();
    /* 每次刷新先清空动态卡*/
    ui_mix_release_visual_content();
    if (result == APP_OK) {
        (void)snprintf(state_text,
                       sizeof(state_text),
                       "\351\205\215\350\215\257\345\256\214\346\210\220 | %u\346\241\266 | \344\270\273\351\205\215\346\226\271:%u | \345\256\271\351\207\217:%luL", /* 配药完成 | %u桶 | 主配方:%u | 容量:%luL */
                       mix_output->summary.total_bucket_count,
                       mix_output->main_batch_count,
                       (unsigned long)(ui_mix_selected_capacity_ml_x10() / 10000U));
    } else {
        (void)snprintf(state_text,
                       sizeof(state_text),
                       "\351\205\215\350\215\257\346\234\252\345\256\214\346\210\220 | \345\256\271\351\207\217:%luL", /* 配药未完成 | 容量:%luL */
                       (unsigned long)(ui_mix_selected_capacity_ml_x10() / 10000U));
    }
    lv_label_set_text(g_ui_ctx.mix_state_label, state_text);
    if (result != APP_OK) {
        /* 创建配药页无数据状卡*/
        ui_mix_add_metric_card(g_ui_ctx.mix_metric_panel, "\347\212\266\346\200\201", "\346\227\240\346\225\260\346\215\256", "\345\205\210\350\277\220\350\241\214\351\205\215\350\215\257", 0, 0); /* 状态，无数据，先运行配药 */
        return;
    }
    /* 写入顶部指标*/
    (void)snprintf(value_text,
                   sizeof(value_text),
                   "%lu.%01luL",
                   (unsigned long)(mix_output->summary.total_liquid_ml_x10 / 10000U),
                   (unsigned long)((mix_output->summary.total_liquid_ml_x10 % 10000U) / 1000U));
    /* 创建总液量指标卡*/
    ui_mix_add_metric_card(g_ui_ctx.mix_metric_panel, "\346\200\273\346\266\262\351\207\217", value_text, "\346\200\273\350\247\210", 8, 8); /* 总液量，总览 */
    (void)snprintf(value_text,
                   sizeof(value_text),
                   "%lu.%01lu/%lu.%01luL",
                   (unsigned long)(mix_output->summary.total_water_ml_x10 / 10000U),
                   (unsigned long)((mix_output->summary.total_water_ml_x10 % 10000U) / 1000U),
                   (unsigned long)(mix_output->summary.total_pesticide_ml_x10 / 10000U),
                   (unsigned long)((mix_output->summary.total_pesticide_ml_x10 % 10000U) / 1000U));
    /* 创建清水和农编号标卡*/
    ui_mix_add_metric_card(g_ui_ctx.mix_metric_panel, "\346\270\205\346\260\264/\345\206\234\350\215\257", value_text, "\346\257\224\344\276\213", 142, 8); /* 清水/农药，比例 */
    (void)snprintf(value_text,
                   sizeof(value_text),
                   "%u",
                   mix_output->summary.total_bucket_count);
    ui_mix_add_metric_card(g_ui_ctx.mix_metric_panel, "\346\241\266\346\225\260", value_text, "\350\207\252\345\212\250\345\210\206\346\241\266", 8, 112); /* 桶数，自动分桶 */
    (void)snprintf(value_text,
                   sizeof(value_text),
                   "%u",
                   mix_output->main_batch_count);
    ui_mix_add_metric_card(g_ui_ctx.mix_metric_panel, "\344\270\273\351\205\215\346\226\271", value_text, "\345\217\230\351\207\217\351\205\215\350\215\257", 142, 112); /* 主配方，变量配药 */
    x = 8;
    y = 8;
    /* 主批次卡片按两列排列 */
    for (i = 0U; i < mix_output->main_batch_count; i++) {
        /* 创建主批次摘要卡*/
        ui_add_main_batch_card(g_ui_ctx.mix_main_visual_panel, &mix_output->main_batch[i], x, y);
        if ((i % 2U) == 0U) {
            x = 292;
        } else {
            x = 8;
            y = (lv_coord_t)(y + 130);
        }
    }
    /* 桶任务和告警分开刷新 */
    ui_mix_render_bucket_page(mix_output);
    if (mix_output->summary.warning_count > 0U) {
        (void)snprintf(warning_text, sizeof(warning_text), "\350\255\246\345\221\212: %s", mix_output->summary.warning_text); /* 警告: %s */
        lv_label_set_text(g_ui_ctx.mix_warning_label, warning_text);
    } else {
        const monitor_output_t *monitor_output;
        monitor_output = app_core_get_monitor_output();
        if ((monitor_output != 0) && (monitor_output->environment_status.suitable == 0U)) {
            lv_label_set_text(g_ui_ctx.mix_warning_label, "\351\243\216\351\231\251\346\217\220\347\244\272: \347\216\257\345\242\203\351\242\204\350\255\246\357\274\214\345\267\262\350\207\252\345\212\250\344\277\256\346\255\243\345\217\230\351\207\217\351\205\215\350\215\257"); /* 风险提示: 环境预警，已自动修正变量配药 */
        } else if (mix_output->summary.total_bucket_count > 1U) {
            lv_label_set_text(g_ui_ctx.mix_warning_label, "\351\243\216\351\231\251\346\217\220\347\244\272: \346\241\266\345\256\271\351\207\217\344\270\215\350\266\263\346\227\266\350\207\252\345\212\250\345\210\206\346\241\266"); /* 风险提示: 桶容量不足时自动分桶 */
        } else {
            lv_label_set_text(g_ui_ctx.mix_warning_label, "\351\243\216\351\231\251\346\217\220\347\244\272: \345\275\223\345\211\215\347\216\257\345\242\203\351\200\202\345\256\234\357\274\214\346\214\211\345\217\230\351\207\217\345\200\215\347\216\207\351\205\215\350\215\257"); /* 风险提示: 当前环境适宜，按变量倍率配药 */
        }
    }
}

/* ---- Plan page ---- */
#define UI_PLAN_CARD_RADIUS 8

#define UI_PLAN_MAP_W 560

#define UI_PLAN_MAP_H 310

#define UI_PLAN_MAP_MAX_PLOT_OBJECTS 64U

static lv_point_t s_plan_path_points[APP_MAX_WAYPOINT_PER_SUB_BATCH];

static lv_point_t s_plan_field_points[5];

/* 处理规划桶任务卡片点击事*/
static void ui_plan_bucket_event_cb(lv_event_t *e);

/* 创建规划页卡*/
static lv_obj_t *ui_plan_create_card(lv_obj_t *parent,
                                lv_coord_t x,
                                lv_coord_t y,
                                lv_coord_t w,
                                lv_coord_t h,
                                lv_color_t accent)
{
    lv_obj_t *card;
    /* 创建规划页卡片容*/
    card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, x, y);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(card, UI_PLAN_CARD_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(0xDDE6DD), LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(card, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 4, LV_PART_MAIN);
    lv_obj_set_style_border_side(card, LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, accent, LV_PART_MAIN);
    return card;
}

/* 添加规划指标*/
static void ui_plan_add_metric_card(lv_obj_t *parent,
                               const char *title,
                               const char *value,
                               const char *hint,
                               lv_coord_t x,
                               lv_coord_t y,
                               lv_coord_t w,
                               lv_coord_t h,
                               lv_color_t accent)
{
    lv_obj_t *card;
    /* 创建规划指标卡片 */
    card = ui_plan_create_card(parent, x, y, w, h, accent);
    /* 创建规划指标标标题标标签 */
    ui_add_label(card, title, 10, 7, (lv_coord_t)(w - 20), 0U, lv_color_hex(0x56645B));
    /* 创建规划指标数标*/
    ui_add_label(card, value, 10, 30, (lv_coord_t)(w - 20), 1U, lv_color_hex(0x1F2A24));
    /* 创建规划指标说明标标签 */
    ui_add_label(card, hint, 10, (lv_coord_t)(h - 22), (lv_coord_t)(w - 20), 0U, lv_color_hex(0x7A877F));
}

/* 添加规划详情*/
static void ui_add_detail_card(lv_obj_t *parent,
                               const char *title,
                               const char *body,
                               const char *hint,
                               lv_coord_t x,
                               lv_coord_t y,
                               lv_coord_t w,
                               lv_coord_t h,
                               lv_color_t accent)
{
    lv_obj_t *card;
    /* 创建规划详情卡片 */
    card = ui_plan_create_card(parent, x, y, w, h, accent);
    /* 创建详情卡片标标题标标签 */
    ui_add_label(card, title, 10, 8, (lv_coord_t)(w - 20), 1U, lv_color_hex(0x445047));
    /* 创建详情卡片正文标标签 */
    ui_add_label(card, body, 10, 34, (lv_coord_t)(w - 20), 0U, lv_color_hex(0x1F2A24));
    /* 创建详情卡片底部提示标标签 */
    ui_add_label(card, hint, 10, (lv_coord_t)(h - 24), (lv_coord_t)(w - 20), 1U, lv_color_hex(0x66716B));
}

/* 添加规划地图图例 */
static void ui_add_map_legend(lv_obj_t *map_panel, lv_color_t route_color)
{
    lv_obj_t *chip;
    if (map_panel == 0) {
        return;
    }
    /* 创建地图图例卡片 */
    chip = ui_plan_create_card(map_panel, 8, 8, 146, 141, lv_palette_lighten(LV_PALETTE_GREY, 2));
    /* 创建当前累加图例文字 */
    ui_add_label(chip, "\351\242\234\350\211\262\345\233\276\344\276\213", 8, 6, 126, 1U, lv_color_hex(0x1F2A24)); /* 颜色图例 */
    ui_add_label(chip, "\350\231\232\347\272\277:\345\275\223\345\211\215\350\267\257\345\276\204", 8, 30, 124, 0U, route_color); /* 虚线:当前路径 */
    ui_add_label(chip, "\346\270\205\346\260\264", 8, 52, 60, 0U, ui_content_color(SPRAY_CONTENT_WATER, DISEASE_TYPE_NONE)); /* 清水 */
    ui_add_label(chip, "\346\236\257\350\220\216\347\227\205", 74, 52, 64, 0U, ui_content_color(SPRAY_CONTENT_PESTICIDE, DISEASE_TYPE_BLIGHT)); /* 枯萎病 */
    ui_add_label(chip, "\351\224\210\347\227\205", 8, 74, 60, 0U, ui_content_color(SPRAY_CONTENT_PESTICIDE, DISEASE_TYPE_RUST)); /* 锈病 */
    ui_add_label(chip, "\350\231\253\345\256\263", 74, 74, 64, 0U, ui_content_color(SPRAY_CONTENT_PESTICIDE, DISEASE_TYPE_INSECT)); /* 虫害 */
    ui_add_label(chip, "\351\234\211\347\227\205", 8, 96, 60, 0U, ui_content_color(SPRAY_CONTENT_PESTICIDE, DISEASE_TYPE_MILDEW)); /* 霉病 */
    /* 创建服务点图例文*/
    ui_add_label(chip, "\350\223\235\347\202\271:\350\241\245\347\273\231\347\202\271", 8, 116, 124, 0U, lv_palette_main(LV_PALETTE_BLUE)); /* 蓝点:补给点 */
}

/* 选择累加显示颜色 */
static void ui_plan_add_efficiency_board(lv_obj_t *map_panel, const plan_output_t *plan_output)
{
    lv_obj_t *card;
    char text[96];
    uint32_t transit_distance_mm;
    if ((map_panel == 0) || (plan_output == 0)) {
        return;
    }
    transit_distance_mm = 0U;
    if (plan_output->summary.total_distance_mm > plan_output->summary.total_spray_distance_mm) {
        transit_distance_mm = plan_output->summary.total_distance_mm - plan_output->summary.total_spray_distance_mm;
    }
    card = ui_plan_create_card(map_panel, 374, 8, 176, 123, lv_palette_main(LV_PALETTE_GREEN));
    ui_add_label(card, "\350\267\257\345\276\204\346\225\210\347\216\207", 10, 8, 150, 1U, lv_color_hex(0x1F2A24)); /* 路径效率 */
    (void)snprintf(text,
                   sizeof(text),
                   "\346\200\273\350\267\235\347\246\273:%lu.%01lum", /* 总距离:%lu.%01lum */
                   (unsigned long)(plan_output->summary.total_distance_mm / 1000U),
                   (unsigned long)((plan_output->summary.total_distance_mm % 1000U) / 100U));
    ui_add_label(card, text, 10, 34, 152, 0U, lv_color_hex(0x333333));
    (void)snprintf(text,
                   sizeof(text),
                   "\345\226\267\346\264\222\350\267\235\347\246\273:%lu.%01lum", /* 喷洒距离:%lu.%01lum */
                   (unsigned long)(plan_output->summary.total_spray_distance_mm / 1000U),
                   (unsigned long)((plan_output->summary.total_spray_distance_mm % 1000U) / 100U));
    ui_add_label(card, text, 10, 56, 152, 0U, lv_color_hex(0x333333));
    (void)snprintf(text,
                   sizeof(text),
                   "\351\235\236\345\226\267\346\264\222:%lu.%01lum", /* 非喷洒:%lu.%01lum */
                   (unsigned long)(transit_distance_mm / 1000U),
                   (unsigned long)((transit_distance_mm % 1000U) / 100U));
    ui_add_label(card, text, 10, 78, 152, 0U, lv_color_hex(0x333333));
    (void)snprintf(text,
                   sizeof(text),
                   "\351\242\204\350\256\241\350\200\227\346\227\266:%lu.%01lus", /* 预计耗时:%lu.%01lus */
                   (unsigned long)(plan_output->summary.total_estimated_time_ms / 1000U),
                   (unsigned long)((plan_output->summary.total_estimated_time_ms % 1000U) / 100U));
    ui_add_label(card, text, 10, 100, 152, 0U, lv_color_hex(0x333333));
}

static lv_color_t ui_plan_path_color(const sub_batch_t *plan_sub_batch)
{
    const mix_output_t *mix_output;
    const mix_sub_batch_t *mix_sub_batch;
    if (plan_sub_batch == 0) {
        return lv_palette_main(LV_PALETTE_GREEN);
    }
    mix_output = app_core_get_mix_output();
    if (plan_sub_batch->mix_sub_batch_index >= mix_output->sub_batch_count) {
        return lv_palette_main(LV_PALETTE_GREEN);
    }
    mix_sub_batch = &mix_output->sub_batch[plan_sub_batch->mix_sub_batch_index];
    return ui_content_color(mix_sub_batch->spray_content, mix_sub_batch->disease_type);
}

/* 把真X 坐标映射到规划地图像*/
static lv_coord_t ui_plan_map_x(const ui_sim_grid_layout_t *layout, int32_t x_mm)
{
    int32_t span_mm;
    if (layout == 0) {
        return 0;
    }
    span_mm = (int32_t)layout->scale_span_mm;
    if (span_mm <= 0) {
        return 8;
    }
    return (lv_coord_t)(layout->origin_x +
                        (((int64_t)(x_mm - layout->min_x_mm) * (int64_t)layout->scale_px) /
                         (int64_t)span_mm));
}

/* 把真Y 坐标映射到规划地图像*/
static lv_coord_t ui_plan_map_y(const ui_sim_grid_layout_t *layout, int32_t y_mm)
{
    int32_t span_mm;
    if (layout == 0) {
        return 0;
    }
    span_mm = (int32_t)layout->scale_span_mm;
    if (span_mm <= 0) {
        return 8;
    }
    return (lv_coord_t)(layout->origin_y +
                        (((int64_t)(y_mm - layout->min_y_mm) * (int64_t)layout->scale_px) /
                         (int64_t)span_mm));
}

/* 计算规划页地图的缩放布局 */
static void ui_plan_calc_map_layout(const monitor_output_t *monitor_output,
                                    const plan_output_t *plan_output,
                                    ui_sim_grid_layout_t *layout)
{
    ui_sim_grid_layout_t base_layout;
    uint32_t scale_x;
    uint32_t scale_y;
    uint32_t scale_px;
    if ((monitor_output == 0) || (plan_output == 0) || (layout == 0)) {
        return;
    }
    ui_sim_calc_grid_layout(monitor_output, plan_output, &base_layout);
    *layout = base_layout;
    scale_x = ((uint32_t)(UI_PLAN_MAP_W - 16) * 1000U) / 620U;
    scale_y = ((uint32_t)(UI_PLAN_MAP_H - 16) * 1000U) / 240U;
    scale_px = (scale_x < scale_y) ? scale_x : scale_y;
    layout->scale_px = (lv_coord_t)(((uint32_t)base_layout.scale_px * scale_px) / 1000U);
    layout->width = (lv_coord_t)(((uint32_t)base_layout.width * scale_px) / 1000U);
    layout->height = (lv_coord_t)(((uint32_t)base_layout.height * scale_px) / 1000U);
    layout->origin_x = (lv_coord_t)((UI_PLAN_MAP_W - layout->width) / 2 - 10);
    layout->origin_y = (lv_coord_t)((UI_PLAN_MAP_H - layout->height) / 2);
}

/* 绘制农田边界 */
static void ui_plan_draw_field_border(lv_obj_t *map_panel,
                                      const monitor_output_t *monitor_output,
                                      const ui_sim_grid_layout_t *layout)
{
    lv_obj_t *line;
    if ((map_panel == 0) || (monitor_output == 0) || (layout == 0) || (monitor_output->field.vertex_count < 4U)) {
        return;
    }
    s_plan_field_points[0].x = ui_plan_map_x(layout, monitor_output->field.vertex[0].x_mm);
    s_plan_field_points[0].y = ui_plan_map_y(layout, monitor_output->field.vertex[0].y_mm);
    s_plan_field_points[1].x = ui_plan_map_x(layout, monitor_output->field.vertex[1].x_mm);
    s_plan_field_points[1].y = ui_plan_map_y(layout, monitor_output->field.vertex[1].y_mm);
    s_plan_field_points[2].x = ui_plan_map_x(layout, monitor_output->field.vertex[2].x_mm);
    s_plan_field_points[2].y = ui_plan_map_y(layout, monitor_output->field.vertex[2].y_mm);
    s_plan_field_points[3].x = ui_plan_map_x(layout, monitor_output->field.vertex[3].x_mm);
    s_plan_field_points[3].y = ui_plan_map_y(layout, monitor_output->field.vertex[3].y_mm);
    s_plan_field_points[4] = s_plan_field_points[0];
    /* 创建农田边界*/
    line = lv_line_create(map_panel);
    lv_line_set_points(line, s_plan_field_points, 5);
    lv_obj_set_style_line_width(line, 2, LV_PART_MAIN);
    lv_obj_set_style_line_color(line, lv_color_hex(0x4B5F51), LV_PART_MAIN);
    lv_obj_set_style_line_rounded(line, 1, LV_PART_MAIN);
}

/* 在规划地图里绘制小地块底*/
static void ui_plan_draw_small_plots(lv_obj_t *map_panel,
                                     const monitor_output_t *monitor_output,
                                     const ui_sim_grid_layout_t *layout)
{
    uint16_t i;
    if ((map_panel == 0) || (monitor_output == 0) || (layout == 0)) {
        return;
    }
    /* 没有足停止地图信息时不绘制底图 */
    if ((monitor_output->plot_count == 0U) ||
        (monitor_output->plot_count > UI_PLAN_MAP_MAX_PLOT_OBJECTS) ||
        (monitor_output->grid.cell_width_mm == 0U) ||
        (monitor_output->grid.cell_height_mm == 0U)) {
        return;
    }
    /* 按监测结果把每块地映射到规划小地*/
    for (i = 0U; i < monitor_output->plot_count; i++) {
        const plot_status_t *plot;
        lv_obj_t *cell;
        lv_coord_t x1;
        lv_coord_t y1;
        lv_coord_t x2;
        lv_coord_t y2;
        lv_coord_t w;
        lv_coord_t h;
        plot = &monitor_output->plot_status[i];
        x1 = ui_plan_map_x(layout, plot->x_mm);
        y1 = ui_plan_map_y(layout, plot->y_mm);
        x2 = ui_plan_map_x(layout, plot->x_mm + (int32_t)monitor_output->grid.cell_width_mm);
        y2 = ui_plan_map_y(layout, plot->y_mm + (int32_t)monitor_output->grid.cell_height_mm);
        w = (lv_coord_t)(x2 - x1);
        h = (lv_coord_t)(y2 - y1);
        /* 小尺寸保证地块仍能显示在地图中 */
        if (w < 3) {
            w = 3;
        }
        if (h < 3) {
            h = 3;
        }
        /* 创建规划地图上的小地块底*/
        cell = lv_obj_create(map_panel);
        lv_obj_set_size(cell, (lv_coord_t)(w - 1), (lv_coord_t)(h - 1));
        lv_obj_set_pos(cell, x1, y1);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(cell, 1, LV_PART_MAIN);
        lv_obj_set_style_border_width(cell, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(cell, lv_palette_lighten(LV_PALETTE_GREY, 2), LV_PART_MAIN);
        lv_obj_set_style_bg_color(cell, ui_plot_color(plot), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, LV_PART_MAIN);
    }
}

/* 绘制服务*/
static void ui_plan_draw_service_point(lv_obj_t *map_panel,
                                       const plan_output_t *plan_output,
                                       const ui_sim_grid_layout_t *layout)
{
    lv_obj_t *service;
    lv_coord_t x;
    lv_coord_t y;
    if ((map_panel == 0) || (plan_output == 0) || (layout == 0)) {
        return;
    }
    x = ui_plan_map_x(layout, plan_output->summary.service_point.x_mm);
    y = ui_plan_map_y(layout, plan_output->summary.service_point.y_mm);
    /* 创建规划地图上的服务点图*/
    service = lv_obj_create(map_panel);
    lv_obj_set_size(service, 16, 16);
    lv_obj_set_pos(service, (lv_coord_t)(x - 8), (lv_coord_t)(y - 8));
    lv_obj_clear_flag(service, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(service, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(service, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(service, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(service, lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(service, LV_OPA_COVER, LV_PART_MAIN);
}

/* 绘制当前选中的路*/
static void ui_plan_draw_selected_path(lv_obj_t *map_panel,
                                       const plan_output_t *plan_output,
                                       const sub_batch_t *sub_batch,
                                       const ui_sim_grid_layout_t *layout,
                                       lv_color_t color)
{
    lv_obj_t *line;
    uint16_t point_count;
    uint16_t i;
    if ((map_panel == 0) || (plan_output == 0) || (sub_batch == 0) || (layout == 0)) {
        return;
    }
    point_count = sub_batch->waypoint_count;
    if (point_count > APP_MAX_WAYPOINT_PER_SUB_BATCH) {
        point_count = APP_MAX_WAYPOINT_PER_SUB_BATCH;
    }
    if ((point_count < 2U) || ((uint32_t)sub_batch->start_index + (uint32_t)point_count > plan_output->waypoint_count)) {
        return;
    }
    for (i = 0U; i < point_count; i++) {
        const waypoint_t *waypoint;
        waypoint = &plan_output->waypoint[sub_batch->start_index + i];
        s_plan_path_points[i].x = ui_plan_map_x(layout, waypoint->point.x_mm);
        s_plan_path_points[i].y = ui_plan_map_y(layout, waypoint->point.y_mm);
    }
    /* 创建选中桶任务的累加折线 */
    line = lv_line_create(map_panel);
    lv_line_set_points(line, s_plan_path_points, point_count);
    lv_obj_set_style_line_width(line, 3, LV_PART_MAIN);
    lv_obj_set_style_line_color(line, color, LV_PART_MAIN);
    lv_obj_set_style_line_dash_width(line, 8, LV_PART_MAIN);
    lv_obj_set_style_line_dash_gap(line, 5, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(line, 1, LV_PART_MAIN);
    lv_obj_move_foreground(line);
}

/* 渲染选中桶任务的累加地图 */
static void ui_plan_render_route_map(uint16_t selected_index)
{
    const monitor_output_t *monitor_output;
    const plan_output_t *plan_output;
    const sub_batch_t *selected_sub_batch;
    ui_sim_grid_layout_t layout;
    lv_obj_t *map_panel;
    lv_color_t color;
    monitor_output = app_core_get_monitor_output();
    plan_output = app_core_get_plan_output();
    /* 没有规划结果时不创建地图面板 */
    if ((g_ui_ctx.plan_metric_panel == 0) ||
        (selected_index >= plan_output->sub_batch_count) ||
        (monitor_output->plot_count == 0U)) {
        return;
    }
    /* 选中桶决定路径颜色和详情内容 */
    selected_sub_batch = &plan_output->sub_batch[selected_index];
    color = ui_plan_path_color(selected_sub_batch);
    /* 创建规划页中的路线地图面*/
    map_panel = lv_obj_create(g_ui_ctx.plan_metric_panel);
    lv_obj_set_size(map_panel, UI_PLAN_MAP_W, UI_PLAN_MAP_H);
    lv_obj_set_pos(map_panel, 10, 10);
    lv_obj_clear_flag(map_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(map_panel, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(map_panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(map_panel, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_MAIN);
    lv_obj_set_style_bg_color(map_panel, lv_color_hex(0xF8FAF7), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(map_panel, LV_OPA_COVER, LV_PART_MAIN);
    /* 地图内容按底图边界服务点、路径顺序叠*/
    ui_plan_calc_map_layout(monitor_output, plan_output, &layout);
    ui_plan_draw_small_plots(map_panel, monitor_output, &layout);
    ui_plan_draw_field_border(map_panel, monitor_output, &layout);
    ui_plan_draw_service_point(map_panel, plan_output, &layout);
    ui_plan_draw_selected_path(map_panel, plan_output, selected_sub_batch, &layout, color);
    ui_add_map_legend(map_panel, color);
    ui_plan_add_efficiency_board(map_panel, plan_output);
}

/* 渲染选中桶任务的文字详情 */
static void ui_plan_render_route_detail(uint16_t selected_index)
{
    const plan_output_t *plan_output;
    const sub_batch_t *selected_sub_batch;
    const mix_output_t *mix_output;
    const mix_sub_batch_t *mix_sub_batch;
    char text[256];
    const char *number_name;
    const char *content_name;
    lv_color_t color;
    plan_output = app_core_get_plan_output();
    mix_output = app_core_get_mix_output();
    if ((g_ui_ctx.plan_detail_panel == 0) || (selected_index >= plan_output->sub_batch_count)) {
        return;
    }
    selected_sub_batch = &plan_output->sub_batch[selected_index];
    mix_sub_batch = 0;
    content_name = "\346\234\252\347\237\245"; /* 未知 */
    if ((mix_output != 0) && (selected_sub_batch->mix_sub_batch_index < mix_output->sub_batch_count)) {
        mix_sub_batch = &mix_output->sub_batch[selected_sub_batch->mix_sub_batch_index];
        content_name = ui_mix_content_name(mix_sub_batch->spray_content, mix_sub_batch->disease_type);
    }
    color = ui_plan_path_color(selected_sub_batch);
    lv_label_set_text(g_ui_ctx.plan_detail_title_label, "\350\267\257\345\276\204\350\257\246\346\203\205"); /* 路径详情 */
    number_name = ui_cn_number_name(selected_sub_batch->sub_batch_id);
    if (number_name != 0) {
        (void)snprintf(text,
                       sizeof(text),
                       "\344\273\273\345\212\241\347\274\226\345\217\267:%s\n\345\257\271\350\261\241:%s\n\350\246\206\347\233\226:%u\345\235\227  \350\210\252\347\202\271:%u\n\350\267\235\347\246\273:%lu.%01lum  \345\226\267\346\264\222:%lu.%01lum\n\351\242\204\350\256\241:%lu.%01lus  \346\234\215\345\212\241\347\202\271:%ld,%ld", /* 任务编号:%s
对象:%s
覆盖:%u块  航点:%u
距离:%lu.%01lum  喷洒:%lu.%01lum
预计:%lu.%01lus  服务点:%ld,%ld */
                       number_name,
                       content_name,
                       (mix_sub_batch != 0) ? mix_sub_batch->chunk_count : 0U,
                       selected_sub_batch->waypoint_count,
                       (unsigned long)(selected_sub_batch->path_distance_mm / 1000U),
                       (unsigned long)((selected_sub_batch->path_distance_mm % 1000U) / 100U),
                       (unsigned long)(selected_sub_batch->spray_distance_mm / 1000U),
                       (unsigned long)((selected_sub_batch->spray_distance_mm % 1000U) / 100U),
                       (unsigned long)(selected_sub_batch->estimated_time_ms / 1000U),
                       (unsigned long)((selected_sub_batch->estimated_time_ms % 1000U) / 100U),
                       (long)plan_output->summary.service_point.x_mm,
                       (long)plan_output->summary.service_point.y_mm);
    } else {
        (void)snprintf(text,
                       sizeof(text),
                       "\344\273\273\345\212\241\347\274\226\345\217\267:%u\n\345\257\271\350\261\241:%s\n\350\246\206\347\233\226:%u\345\235\227  \350\210\252\347\202\271:%u\n\350\267\235\347\246\273:%lu.%01lum  \345\226\267\346\264\222:%lu.%01lum\n\351\242\204\350\256\241:%lu.%01lus  \346\234\215\345\212\241\347\202\271:%ld,%ld", /* 任务编号:%u
对象:%s
覆盖:%u块  航点:%u
距离:%lu.%01lum  喷洒:%lu.%01lum
预计:%lu.%01lus  服务点:%ld,%ld */
                       selected_sub_batch->sub_batch_id,
                       content_name,
                       (mix_sub_batch != 0) ? mix_sub_batch->chunk_count : 0U,
                       selected_sub_batch->waypoint_count,
                       (unsigned long)(selected_sub_batch->path_distance_mm / 1000U),
                       (unsigned long)((selected_sub_batch->path_distance_mm % 1000U) / 100U),
                       (unsigned long)(selected_sub_batch->spray_distance_mm / 1000U),
                       (unsigned long)((selected_sub_batch->spray_distance_mm % 1000U) / 100U),
                       (unsigned long)(selected_sub_batch->estimated_time_ms / 1000U),
                       (unsigned long)((selected_sub_batch->estimated_time_ms % 1000U) / 100U),
                       (long)plan_output->summary.service_point.x_mm,
                       (long)plan_output->summary.service_point.y_mm);
    }
    /* 创建当前累加详情卡片 */
    ui_add_detail_card(g_ui_ctx.plan_detail_panel, "\350\267\257\345\276\204\350\247\243\346\236\220", text, "", 8, 8, 300, 250, color); /* 路径解析 */
}

/* 刷新规划页右侧状态*/
static void ui_plan_refresh_detail(void)
{
    ui_clean_container(g_ui_ctx.plan_metric_panel);
    ui_clean_container(g_ui_ctx.plan_detail_panel);
    ui_plan_render_route_map(g_ui_ctx.selected_plan_sub_batch_index);
    ui_plan_render_route_detail(g_ui_ctx.selected_plan_sub_batch_index);
}

/* 清空规划页动态内*/
void ui_plan_release_heavy_text(void)
{
    ui_clean_container(g_ui_ctx.plan_metric_panel);
    ui_clean_container(g_ui_ctx.plan_timeline_panel);
    ui_clean_container(g_ui_ctx.plan_detail_panel);
    if (g_ui_ctx.plan_detail_title_label != 0) {
        lv_label_set_text(g_ui_ctx.plan_detail_title_label, "\345\267\262\351\200\211\350\267\257\345\276\204"); /* 已选路径 */
    }
}

/* 切回规划页时按当前状态重建内*/
void ui_plan_reload_view(void)
{
    const plan_output_t *plan_output;
    app_state_t state;
    plan_output = app_core_get_plan_output();
    state = app_core_get_state();
    if ((plan_output->waypoint_count > 0U) &&
        ((state == APP_STATE_PLAN_DONE) || (state == APP_STATE_SIM_RUNNING) || (state == APP_STATE_SIM_DONE))) {
        ui_plan_update_view(APP_OK);
    } else {
        ui_plan_update_view(APP_ERR_STATE);
    }
}

/* 添加控制点击的规划桶任务卡片 */
static void ui_add_plan_bucket_block(const plan_output_t *plan_output,
                                     uint16_t plan_index,
                                     lv_coord_t x,
                                     lv_coord_t y)
{
    const sub_batch_t *plan_sub_batch;
    lv_obj_t *card;
    char text[96];
    const char *number_name;
    lv_color_t color;
    plan_sub_batch = &plan_output->sub_batch[plan_index];
    color = ui_plan_path_color(plan_sub_batch);
    /* 创建药击的规划桶任务卡*/
    card = ui_plan_create_card(g_ui_ctx.plan_timeline_panel, x, y, 136, 54, color);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, ui_plan_bucket_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)plan_index);
    if (plan_index == g_ui_ctx.selected_plan_sub_batch_index) {
        lv_obj_set_style_border_width(card, 3, LV_PART_MAIN);
        lv_obj_set_style_border_side(card, LV_BORDER_SIDE_FULL, LV_PART_MAIN);
        lv_obj_set_style_bg_color(card, lv_color_hex(0xEEF6EA), LV_PART_MAIN);
    }
    {
        const char *prefix = ui_get_sub_batch_prefix(plan_sub_batch->sub_batch_id);
        number_name = ui_cn_number_name(plan_sub_batch->sub_batch_id);
        if (number_name != 0) {
            (void)snprintf(text, sizeof(text), "%s\344\273\273\345\212\241%s", prefix, number_name); /* %s任务%s */
        } else {
            (void)snprintf(text, sizeof(text), "%s\344\273\273\345\212\241%u", prefix, plan_sub_batch->sub_batch_id); /* %s任务%u */
        }
    }
    /* 创建规划桶任务编号标*/
    ui_add_label(card, text, 8, 2, 112, 1U, color);
    (void)snprintf(text,
                   sizeof(text),
                   "%lu.%01lum",
                   (unsigned long)(plan_sub_batch->path_distance_mm / 1000U),
                   (unsigned long)((plan_sub_batch->path_distance_mm % 1000U) / 100U));
    /* 创建规划桶任务距离标*/
    ui_add_label(card, text, 8, 28, 116, 0U, lv_color_hex(0x333333));
}

/* 渲染规划桶任务列*/
static void ui_plan_render_bucket_page(const plan_output_t *plan_output)
{
    uint16_t i;
    lv_coord_t x;
    char text[96];
    ui_clean_container(g_ui_ctx.plan_timeline_panel);
    if ((g_ui_ctx.plan_timeline_panel == 0) || (plan_output == 0)) {
        return;
    }
    if (plan_output->sub_batch_count == 0U) {
        /* 创建规划任务空状态标*/
        ui_add_label(g_ui_ctx.plan_timeline_panel, "\346\232\202\346\227\240\350\267\257\345\276\204\344\273\273\345\212\241", 8, 8, 220, 1U, lv_color_hex(0x333333)); /* 暂无路径任务 */
        return;
    }
    (void)snprintf(text,
                   sizeof(text),
                   "\350\267\257\345\276\204\344\273\273\345\212\241  \345\205\261%u\346\256\265", /* 路径任务  共%u段 */
                   plan_output->sub_batch_count);
    /* 创建规划任务列表标标题标标签 */
    ui_add_label(g_ui_ctx.plan_timeline_panel, text, 8, 2, 360, 1U, lv_color_hex(0x1F2A24));
    for (i = 0U; i < plan_output->sub_batch_count; i++) {
        x = (lv_coord_t)(8 + i * 144U);
        ui_add_plan_bucket_block(plan_output, i, x, 30);
    }
}

/* 渲染规划页当前内*/
static void ui_plan_render_active_view(const plan_output_t *plan_output)
{
    ui_plan_refresh_detail();
    ui_plan_render_bucket_page(plan_output);
}

/* 刷新规划状地图和累加列表 */
void ui_plan_update_view(app_result_t result)
{
    const plan_output_t *plan_output;
    char state_text[128];
    plan_output = app_core_get_plan_output();
    ui_plan_release_heavy_text();
    if (result == APP_OK) {
        (void)snprintf(state_text,
                       sizeof(state_text),
                       "\350\247\204\345\210\222\345\256\214\346\210\220 | \350\267\257\345\276\204:%u | \350\210\252\347\202\271:%u/%u", /* 规划完成 | 路径:%u | 航点:%u/%u */
                       plan_output->sub_batch_count,
                       plan_output->waypoint_count,
                       APP_MAX_WAYPOINT_COUNT);
    } else {
        (void)snprintf(state_text, sizeof(state_text), "\350\247\204\345\210\222\346\234\252\345\260\261\347\273\252 | \350\257\267\345\205\210\345\256\214\346\210\220\351\205\215\350\215\257 | \347\240\201:%d", (int)result); /* 规划未就绪 | 请先完成配药 | 码:%d */
    }
    lv_label_set_text(g_ui_ctx.plan_state_label, state_text);
    if (result != APP_OK) {
        /* 创建规划控制成时的状态提示卡*/
        ui_plan_add_metric_card(g_ui_ctx.plan_metric_panel, "\347\212\266\346\200\201", "\346\227\240\346\225\260\346\215\256", "\345\205\210\350\277\220\350\241\214\350\247\204\345\210\222", 10, 10, 220, 82, lv_palette_main(LV_PALETTE_GREY)); /* 状态，无数据，先运行规划 */
        return;
    }
    if ((plan_output->sub_batch_count > 0U) &&
        (g_ui_ctx.selected_plan_sub_batch_index >= plan_output->sub_batch_count)) {
        g_ui_ctx.selected_plan_sub_batch_index = 0U;
    }
    ui_plan_render_active_view(plan_output);
}

/* 处理规划桶任务点*/
static void ui_plan_bucket_event_cb(lv_event_t *e)
{
    uint32_t index;
    index = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    if (index < app_core_get_plan_output()->sub_batch_count) {
        g_ui_ctx.selected_plan_sub_batch_index = (uint16_t)index;
        ui_plan_refresh_detail();
        ui_plan_render_bucket_page(app_core_get_plan_output());
    }
}

/* ---- Simulation map ---- */

/* 计算仿真地图缩放布局 */
void ui_sim_calc_grid_layout(const monitor_output_t *monitor_output,
                                    const plan_output_t *plan_output,
                                    ui_sim_grid_layout_t *layout)
{
    int32_t pad_mm;
    uint32_t span_x_mm;
    uint32_t span_y_mm;
    uint32_t scale_span_mm;
    lv_coord_t view_w;
    lv_coord_t view_h;
    /*
     * 鍦板浘鑼冨洿鍖呭惈鍐滅敯鍜屾湇鍔＄偣锛沊/Y 鐢ㄥ悓涓€姣斾緥缂╂斁
     */
    if ((monitor_output == 0) || (layout == 0) || (monitor_output->field.vertex_count < 4U)) {
        return;
    }
    pad_mm = (int32_t)(monitor_output->grid.cell_width_mm / 4U);
    if (pad_mm < 3000) {
        pad_mm = 3000;
    }
    layout->min_x_mm = monitor_output->field.vertex[0].x_mm;
    layout->max_x_mm = monitor_output->field.vertex[2].x_mm;
    layout->min_y_mm = monitor_output->field.vertex[0].y_mm;
    layout->max_y_mm = monitor_output->field.vertex[2].y_mm;
    if (plan_output != 0) {
        if (plan_output->summary.service_point.x_mm < layout->min_x_mm) {
            layout->min_x_mm = plan_output->summary.service_point.x_mm;
        }
        if (plan_output->summary.service_point.x_mm > layout->max_x_mm) {
            layout->max_x_mm = plan_output->summary.service_point.x_mm;
        }
        if (plan_output->summary.service_point.y_mm < layout->min_y_mm) {
            layout->min_y_mm = plan_output->summary.service_point.y_mm;
        }
        if (plan_output->summary.service_point.y_mm > layout->max_y_mm) {
            layout->max_y_mm = plan_output->summary.service_point.y_mm;
        }
    }
    layout->min_x_mm -= pad_mm;
    layout->max_x_mm += pad_mm;
    layout->min_y_mm -= pad_mm;
    layout->max_y_mm += pad_mm;
    span_x_mm = (uint32_t)(layout->max_x_mm - layout->min_x_mm);
    span_y_mm = (uint32_t)(layout->max_y_mm - layout->min_y_mm);
    if ((span_x_mm == 0U) || (span_y_mm == 0U)) {
        layout->origin_x = 8;
        layout->origin_y = 8;
        layout->width = 620;
        layout->height = 240;
        layout->scale_px = 620;
        layout->scale_span_mm = span_x_mm;
        return;
    }
    if (((uint64_t)span_x_mm * 240ULL) >= ((uint64_t)span_y_mm * 620ULL)) {
        /* 宽度占主导时按服务度缩放，高度居中 */
        scale_span_mm = span_x_mm;
        view_w = 620;
        layout->scale_px = 620;
        view_h = (lv_coord_t)(((uint64_t)span_y_mm * 620ULL) / (uint64_t)span_x_mm);
    } else {
        scale_span_mm = span_y_mm;
        view_h = 240;
        layout->scale_px = 240;
        view_w = (lv_coord_t)(((uint64_t)span_x_mm * 240ULL) / (uint64_t)span_y_mm);
    }
    if (view_w < 1) {
        view_w = 1;
    }
    if (view_h < 1) {
        view_h = 1;
    }
    layout->origin_x = (lv_coord_t)(8 + ((620 - view_w) / 2));
    layout->origin_y = (lv_coord_t)(8 + ((240 - view_h) / 2));
    layout->width = view_w;
    layout->height = view_h;
    layout->scale_span_mm = scale_span_mm;
}

/* 把真X 坐标映射到仿真地图像*/
lv_coord_t ui_sim_map_x(const ui_sim_grid_layout_t *layout, int32_t x_mm)
{
    int32_t span_mm;
    if (layout == 0) {
        return 0;
    }
    span_mm = (int32_t)layout->scale_span_mm;
    if (span_mm <= 0) {
        return layout->origin_x;
    }
    return (lv_coord_t)(layout->origin_x +
                        (((int64_t)(x_mm - layout->min_x_mm) * (int64_t)layout->scale_px) /
                         (int64_t)span_mm));
}

/* 把真Y 坐标映射到仿真地图像*/
lv_coord_t ui_sim_map_y(const ui_sim_grid_layout_t *layout, int32_t y_mm)
{
    int32_t span_mm;
    if (layout == 0) {
        return 0;
    }
    span_mm = (int32_t)layout->scale_span_mm;
    if (span_mm <= 0) {
        return layout->origin_y;
    }
    return (lv_coord_t)(layout->origin_y +
                        (((int64_t)(y_mm - layout->min_y_mm) * (int64_t)layout->scale_px) /
                         (int64_t)span_mm));
}

/* 计算两个航点之间的距*/
static uint32_t ui_sim_segment_distance_mm(const waypoint_t *from, const waypoint_t *to)
{
    if ((from == 0) || (to == 0)) {
        return 0U;
    }
    return app_point_distance_mm(&from->point, &to->point);
}

/* 返回两个 int32 的较小*/
static int32_t ui_sim_i32_min(int32_t a, int32_t b)
{
    return (a < b) ? a : b;
}

/* 返回两个 int32 的较大*/
static int32_t ui_sim_i32_max(int32_t a, int32_t b)
{
    return (a > b) ? a : b;
}

/* 计算两个维区间的重叠长度 */
static uint32_t ui_sim_overlap_1d_mm(int32_t a_min, int32_t a_max, int32_t b_min, int32_t b_max)
{
    int32_t left;
    int32_t right;
    left = ui_sim_i32_max(a_min, b_min);
    right = ui_sim_i32_min(a_max, b_max);
    if (right <= left) {
        return 0U;
    }
    return (uint32_t)(right - left);
}

/* 估算当前喷洒航线对单个地块的覆盖率 */
static uint32_t ui_sim_spray_segment_overlap_mm(const waypoint_t *from,
                                                const waypoint_t *to,
                                                const mix_chunk_t *chunk,
                                                uint32_t segment_done_mm)
{
    uint32_t segment_mm;
    uint32_t overlap_mm;
    int32_t x_min;
    int32_t x_max;
    int32_t y_min;
    int32_t y_max;
    int32_t done_x;
    int32_t done_y;
    if ((from == 0) || (to == 0) || (chunk == 0) || (segment_done_mm == 0U)) {
        return 0U;
    }
    segment_mm = ui_sim_segment_distance_mm(from, to);
    if (segment_mm == 0U) {
        return 0U;
    }
    if (segment_done_mm > segment_mm) {
        segment_done_mm = segment_mm;
    }
    overlap_mm = 0U;
    /* 规划航线只在水平或竖白段估算覆盖 */
    if (from->point.y_mm == to->point.y_mm) {
        /* 水平段按 x 方向计算已完成网格*/
        if ((from->point.y_mm >= chunk->min_y_mm) && (from->point.y_mm <= chunk->max_y_mm)) {
            if (to->point.x_mm >= from->point.x_mm) {
                done_x = from->point.x_mm + (int32_t)segment_done_mm;
            } else {
                done_x = from->point.x_mm - (int32_t)segment_done_mm;
            }
            x_min = ui_sim_i32_min(from->point.x_mm, done_x);
            x_max = ui_sim_i32_max(from->point.x_mm, done_x);
            overlap_mm = ui_sim_overlap_1d_mm(x_min, x_max, chunk->min_x_mm, chunk->max_x_mm);
        }
    } else if (from->point.x_mm == to->point.x_mm) {
        /* 垂直段按 y 方向计算已完成网格*/
        if ((from->point.x_mm >= chunk->min_x_mm) && (from->point.x_mm <= chunk->max_x_mm)) {
            if (to->point.y_mm >= from->point.y_mm) {
                done_y = from->point.y_mm + (int32_t)segment_done_mm;
            } else {
                done_y = from->point.y_mm - (int32_t)segment_done_mm;
            }
            y_min = ui_sim_i32_min(from->point.y_mm, done_y);
            y_max = ui_sim_i32_max(from->point.y_mm, done_y);
            overlap_mm = ui_sim_overlap_1d_mm(y_min, y_max, chunk->min_y_mm, chunk->max_y_mm);
        }
    }
    return overlap_mm;
}

/* 估算当前桶对某个地块已完成的喷洒覆盖率 */
static uint32_t ui_sim_chunk_spray_mm(const plan_output_t *plan_output,
                                      const sub_batch_t *sub_batch,
                                      const mix_chunk_t *chunk,
                                      uint32_t sub_route_done_mm)
{
    uint16_t i;
    uint16_t start_index;
    uint16_t end_index;
    uint32_t route_acc_mm;
    uint32_t chunk_spray_mm;
    if ((plan_output == 0) || (sub_batch == 0) || (chunk == 0) || (sub_batch->waypoint_count < 2U)) {
        return 0U;
    }
    start_index = sub_batch->start_index;
    end_index = (uint16_t)(sub_batch->start_index + sub_batch->waypoint_count);
    /* 子路线范围不能超过全航点数组 */
    if (end_index > plan_output->waypoint_count) {
        end_index = plan_output->waypoint_count;
    }
    route_acc_mm = 0U;
    chunk_spray_mm = 0U;
    /* 沿子批次路径已经走过的喷洒段 */
    for (i = (uint16_t)(start_index + 1U); i < end_index; i++) {
        const waypoint_t *from;
        const waypoint_t *to;
        uint32_t segment_mm;
        uint32_t segment_done_mm;
        from = &plan_output->waypoint[i - 1U];
        to = &plan_output->waypoint[i];
        segment_mm = ui_sim_segment_distance_mm(from, to);
        if (segment_mm == 0U) {
            continue;
        }
        if (sub_route_done_mm <= route_acc_mm) {
            break;
        }
        segment_done_mm = sub_route_done_mm - route_acc_mm;
        if (segment_done_mm > segment_mm) {
            segment_done_mm = segment_mm;
        }
        /* 航点累计喷洒量 */
        if (to->action == PLAN_ACTION_SPRAY_ON) {
            chunk_spray_mm += ui_sim_spray_segment_overlap_mm(from, to, chunk, segment_done_mm);
        }
        route_acc_mm += segment_mm;
    }
    return chunk_spray_mm;
}

/* 根据仿真进度更新已喷完地块的显示 */
static void ui_sim_update_completed_plots(void)
{
    const monitor_output_t *monitor_output;
    const mix_output_t *mix_output;
    const plan_output_t *plan_output;
    const sim_output_t *sim_output;
    uint16_t *total_chunks;
    uint16_t *done_chunks;
    uint16_t sub_index;
    uint16_t plot_index;
    uint32_t route_start_mm;
    if (g_ui_ctx.sim_grid_panel == 0) {
        return;
    }
    monitor_output = app_core_get_monitor_output();
    mix_output = app_core_get_mix_output();
    plan_output = app_core_get_plan_output();
    sim_output = app_core_get_sim_output();
    if ((monitor_output->plot_count == 0U) || (mix_output->sub_batch_count == 0U) ||
        (plan_output->sub_batch_count == 0U)) {
        return;
    }
    total_chunks = g_ui_sim_total_chunks;
    done_chunks = g_ui_sim_done_chunks;
    (void)memset(total_chunks, 0, sizeof(g_ui_sim_total_chunks));
    (void)memset(done_chunks, 0, sizeof(g_ui_sim_done_chunks));
    /* 根据无人机在仿真路径上的位置和喷洒范围，更新各个地块的已喷洒药量和状态 */
    route_start_mm = 0U;
    for (sub_index = 0U; sub_index < plan_output->sub_batch_count; sub_index++) {
        const sub_batch_t *plan_sub_batch;
        const mix_sub_batch_t *mix_sub_batch;
        uint32_t sub_route_done_mm;
        uint16_t chunk_index;
        plan_sub_batch = &plan_output->sub_batch[sub_index];
        if (plan_sub_batch->mix_sub_batch_index >= mix_output->sub_batch_count) {
            route_start_mm += plan_sub_batch->path_distance_mm;
            continue;
        }
        mix_sub_batch = &mix_output->sub_batch[plan_sub_batch->mix_sub_batch_index];
        if (sim_output->route_distance_done_mm <= route_start_mm) {
            sub_route_done_mm = 0U;
        } else {
            sub_route_done_mm = sim_output->route_distance_done_mm - route_start_mm;
            if (sub_route_done_mm > plan_sub_batch->path_distance_mm) {
                sub_route_done_mm = plan_sub_batch->path_distance_mm;
            }
        }
        for (chunk_index = 0U; chunk_index < mix_sub_batch->chunk_count; chunk_index++) {
            const mix_chunk_t *chunk;
            uint16_t chunk_plot_index;
            uint32_t chunk_total_spray_mm;
            uint32_t chunk_done_spray_mm;
            chunk = &mix_sub_batch->chunk[chunk_index];
            if ((chunk->plot_id == 0U) || (chunk->plot_id > monitor_output->plot_count)) {
                continue;
            }
            chunk_plot_index = (uint16_t)(chunk->plot_id - 1U);
            total_chunks[chunk_plot_index]++;
            chunk_total_spray_mm =
                ui_sim_chunk_spray_mm(plan_output, plan_sub_batch, chunk, plan_sub_batch->path_distance_mm);
            chunk_done_spray_mm =
                ui_sim_chunk_spray_mm(plan_output, plan_sub_batch, chunk, sub_route_done_mm);
            if ((chunk_total_spray_mm > 0U) && (chunk_done_spray_mm >= chunk_total_spray_mm)) {
                done_chunks[chunk_plot_index]++;
            }
        }
        route_start_mm += plan_sub_batch->path_distance_mm;
    }
    for (plot_index = 0U; plot_index < monitor_output->plot_count; plot_index++) {
        if (g_ui_ctx.sim_grid_cell[plot_index] == 0) {
            continue;
        }
        if ((total_chunks[plot_index] > 0U) && (done_chunks[plot_index] >= total_chunks[plot_index])) {
            lv_obj_set_style_bg_color(g_ui_ctx.sim_grid_cell[plot_index],
                                      lv_palette_main(LV_PALETTE_GREEN),
                                      LV_PART_MAIN);
        } else {
            lv_obj_set_style_bg_color(g_ui_ctx.sim_grid_cell[plot_index],
                                      ui_plot_color(&monitor_output->plot_status[plot_index]),
                                      LV_PART_MAIN);
        }
    }
}

/* 把当前仿真坐标同步到飞机图标 */
static void ui_sim_update_plane_on_grid(lv_obj_t *panel, lv_obj_t *plane_icon)
{
    const monitor_output_t *monitor_output;
    const plan_output_t *plan_output;
    const sim_output_t *sim_output;
    ui_sim_grid_layout_t layout;
    lv_coord_t plane_x;
    lv_coord_t plane_y;
    /* 更新仿真网格地图上无人机图标的实时位置与旋转姿态 */
    if ((panel == 0) || (plane_icon == 0)) {
        return;
    }
    monitor_output = app_core_get_monitor_output();
    plan_output = app_core_get_plan_output();
    sim_output = app_core_get_sim_output();
    if ((app_core_get_state() != APP_STATE_SIM_RUNNING) ||
        (monitor_output->plot_count == 0U) ||
        (monitor_output->field.vertex_count < 4U) ||
        (monitor_output->field.vertex[2].x_mm <= monitor_output->field.vertex[0].x_mm) ||
        (monitor_output->field.vertex[2].y_mm <= monitor_output->field.vertex[0].y_mm)) {
        lv_obj_add_flag(plane_icon, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if ((monitor_output->grid.rows == 0U) || (monitor_output->grid.cols == 0U)) {
        lv_obj_add_flag(plane_icon, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    ui_sim_calc_grid_layout(monitor_output, plan_output, &layout);
    plane_x = ui_sim_map_x(&layout, sim_output->current_position.x_mm);
    plane_y = ui_sim_map_y(&layout, sim_output->current_position.y_mm);
    if ((plane_x < layout.origin_x) || (plane_y < layout.origin_y) ||
        (plane_x > (layout.origin_x + layout.width)) || (plane_y > (layout.origin_y + layout.height))) {
        lv_obj_add_flag(plane_icon, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_set_pos(plane_icon, plane_x - 10, plane_y - 10);
    lv_obj_clear_flag(plane_icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(plane_icon);
}

/* 刷新仿真地图上的覆盖状和飞机位置 */
void ui_sim_update_map(void)
{
    ui_sim_update_completed_plots();
    ui_sim_update_plane_on_grid(g_ui_ctx.sim_grid_panel, g_ui_ctx.sim_grid_plane_icon);
}

/* 刷新仿真进度条和进度文字 */
void ui_sim_update_progress_bar(const sim_output_t *sim_output)
{
    char progress_text[48];
    char battery_text[48];
    uint16_t progress_x100;
    uint16_t battery_x100;
    if ((g_ui_ctx.sim_progress_bar == 0) || (g_ui_ctx.sim_progress_label == 0) || (sim_output == 0)) {
        return;
    }
    /* progress_x100 正好对应 LVGL bar 0..10000 范围 */
    progress_x100 = sim_output->progress_x100;
    if (progress_x100 > 10000U) {
        progress_x100 = 10000U;
    }
    lv_bar_set_value(g_ui_ctx.sim_progress_bar, (int32_t)progress_x100, LV_ANIM_OFF);
    (void)snprintf(progress_text,
                   sizeof(progress_text),
                   "\350\267\257\347\272\277\350\277\233\345\272\246\357\274\232%u.%02u%%", /* 路线进度：%u.%02u%% */
                   progress_x100 / 100U,
                   progress_x100 % 100U);
    lv_label_set_text(g_ui_ctx.sim_progress_label, progress_text);
    if ((g_ui_ctx.sim_battery_bar == 0) || (g_ui_ctx.sim_battery_label == 0)) {
        return;
    }
    battery_x100 = sim_output->battery_x100;
    if (battery_x100 > 10000U) {
        battery_x100 = 10000U;
    }
    if (battery_x100 < 2000U) {
        lv_obj_set_style_bg_color(g_ui_ctx.sim_battery_bar, lv_palette_main(LV_PALETTE_RED), LV_PART_INDICATOR);
    } else if (battery_x100 < 4000U) {
        lv_obj_set_style_bg_color(g_ui_ctx.sim_battery_bar, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_INDICATOR);
    } else {
        lv_obj_set_style_bg_color(g_ui_ctx.sim_battery_bar, lv_palette_main(LV_PALETTE_GREEN), LV_PART_INDICATOR);
    }
    lv_bar_set_value(g_ui_ctx.sim_battery_bar, (int32_t)battery_x100, LV_ANIM_ON);
    (void)snprintf(battery_text,
                   sizeof(battery_text),
                   "\347\224\265\351\207\217\357\274\232%u.%02u%%", /* 电量：%u.%02u%% */
                   battery_x100 / 100U,
                   battery_x100 % 100U);
    lv_label_set_text(g_ui_ctx.sim_battery_label, battery_text);
}

/* ---- Simulation page ---- */

/* 刷新仿真页状态地图进度和报警 */
void ui_sim_update_view(app_result_t result)
{
    app_state_t state;
    const monitor_output_t *monitor_output;
    const mix_output_t *mix_output;
    const plan_output_t *plan_output;
    const sim_output_t *sim_output;
    char state_text[160];
    char summary_text[640];
    char current_text[320];
    char alarm_text[128];
    const char *environment_text;
    const waypoint_t *current_waypoint;
    const char *task_number_name;
    uint16_t current_index;
    state = app_core_get_state();
    monitor_output = app_core_get_monitor_output();
    mix_output = app_core_get_mix_output();
    plan_output = app_core_get_plan_output();
    sim_output = app_core_get_sim_output();
    environment_text = (monitor_output->environment_status.suitable != 0U) ? "\351\200\202\345\256\234" : "\351\242\204\350\255\246"; /* 适宜，预警 */
    if (g_ui_ctx.sim_state_label == 0) {
        return;
    }
    if (result == APP_OK) {
        const char *state_str = "\346\234\252\350\277\220\350\241\214"; /* 未运行 */
        if ((sim_output->running != 0U) || (sim_output->state == SIM_STATE_PAUSED)) {
            switch (sim_output->state) {
                case SIM_STATE_SPRAYING:
                    state_str = "\346\255\243\345\270\270\344\275\234\344\270\232\344\270\255"; /* 正常作业中 */
                    break;
                case SIM_STATE_RETURNING_TO_HOME:
                    if (sim_output->rth_trigger_source == 3U) {
                        state_str = "\344\275\216\347\224\265\350\277\224\350\210\252\345\205\205\347\224\265\344\270\255"; /* 低电返航充电中 */
                    } else {
                        state_str = (sim_output->rth_trigger_source == 1U) ? "\350\207\252\345\212\250\350\277\224\350\210\252\344\270\255" : "\346\211\213\345\212\250\345\217\254\345\233\236\344\270\255"; /* 自动返航中，手动召回中 */
                    }
                    break;
                case SIM_STATE_REFILLING:
                    state_str = (sim_output->rth_trigger_source == 3U) ? "\346\234\215\345\212\241\347\202\271\345\205\205\347\224\265\344\270\255" : "\346\234\215\345\212\241\347\202\271\345\212\240\350\215\257\344\270\255"; /* 服务点充电中，服务点加药中 */
                    break;
                case SIM_STATE_RETURNING_TO_BREAKPOINT:
                    state_str = "\350\207\252\345\212\250\350\277\224\345\233\236\346\226\255\347\202\271\347\273\255\345\226\267\344\270\255"; /* 自动返回断点续喷中 */
                    break;
                case SIM_STATE_PAUSED:
                    state_str = "\344\273\277\347\234\237\346\232\202\345\201\234"; /* 仿真暂停 */
                    break;
                default:
                    state_str = "\350\277\220\350\241\214\344\270\255"; /* 运行中 */
                    break;
            }
        }
        if (sim_output->running != 0U && sim_output->state == SIM_STATE_REFILLING) {
            (void)snprintf(state_text,
                           sizeof(state_text),
                           "\344\273\277\347\234\237\347\212\266\346\200\201: %s | \345\200\222\350\256\241\346\227\266: %lu.%01lus | \347\224\265\351\207\217:%u.%02u%%", /* 仿真状态: %s | 倒计时: %lu.%01lus | 电量:%u.%02u%% */
                           state_str,
                           (unsigned long)(sim_output->refill_timer_ms / 1000U),
                           (unsigned long)((sim_output->refill_timer_ms % 1000U) / 100U),
                           sim_output->battery_x100 / 100U,
                           sim_output->battery_x100 % 100U);
        } else {
            (void)snprintf(state_text,
                           sizeof(state_text),
                           "\344\273\277\347\234\237\347\212\266\346\200\201: %s | \347\224\265\351\207\217:%u.%02u%%", /* 仿真状态: %s | 电量:%u.%02u%% */
                           state_str,
                           sim_output->battery_x100 / 100U,
                           sim_output->battery_x100 % 100U);
        }
    } else if (((state == APP_STATE_PLAN_DONE) || (state == APP_STATE_SIM_DONE)) &&
               (monitor_output->plot_count > 0U) &&
               (monitor_output->environment_status.wind_ok == 0U)) {
        (void)snprintf(state_text,
                       sizeof(state_text),
                       "\344\273\277\347\234\237\347\212\266\346\200\201: \347\246\201\351\243\236\344\277\235\346\212\244 | \351\243\216\351\200\237%u.%um/s\350\266\205\351\231\220", /* 仿真状态: 禁飞保护 | 风速%u.%um/s超限 */
                       monitor_output->environment_status.input.wind_speed_x10 / 10U,
                       monitor_output->environment_status.input.wind_speed_x10 % 10U);
    } else {
        (void)snprintf(state_text,
                       sizeof(state_text),
                       "\344\273\277\347\234\237\347\212\266\346\200\201: \346\234\252\345\260\261\347\273\252 | \345\275\223\345\211\215:%s | \347\240\201:%d", /* 仿真状态: 未就绪 | 当前:%s | 码:%d */
                       ui_app_state_name(state),
                       (int)result);
    }
    lv_label_set_text(g_ui_ctx.sim_state_label, state_text);
    if (result != APP_OK) {
        if (((state == APP_STATE_PLAN_DONE) || (state == APP_STATE_SIM_DONE)) &&
            (monitor_output->plot_count > 0U) &&
            (monitor_output->environment_status.wind_ok == 0U)) {
            lv_label_set_text(g_ui_ctx.sim_summary_label, "\347\246\201\351\243\236\344\277\235\346\212\244\n\347\216\257\345\242\203\351\243\216\351\200\237\350\266\205\350\277\207\344\275\234\347\211\251\344\275\234\344\270\232\344\270\212\351\231\220"); /* 禁飞保护
环境风速超过作物作业上限 */
            lv_label_set_text(g_ui_ctx.sim_alarm_label, "[\344\277\235\346\212\244]\n\351\243\216\351\200\237\350\266\205\351\231\220\357\274\214\347\246\201\346\255\242\350\265\267\351\243\236"); /* [保护]
风速超限，禁止起飞 */
        } else {
            lv_label_set_text(g_ui_ctx.sim_summary_label, "\346\227\240\344\273\277\347\234\237\346\225\260\346\215\256\n\350\257\267\345\205\210\345\256\214\346\210\220\350\267\257\345\276\204\350\247\204\345\210\222"); /* 无仿真数据
请先完成路径规划 */
            lv_label_set_text(g_ui_ctx.sim_alarm_label, "[\346\212\245\350\255\246]\n\346\227\240"); /* [报警]
无 */
        }
        lv_label_set_text(g_ui_ctx.sim_current_label, "[\345\275\223\345\211\215\344\273\273\345\212\241]\n\346\227\240"); /* [当前任务]
无 */
        lv_bar_set_value(g_ui_ctx.sim_progress_bar, 0, LV_ANIM_OFF);
        lv_label_set_text(g_ui_ctx.sim_progress_label, "\350\267\257\347\272\277\350\277\233\345\272\246: 0.00%"); /* 路线进度: 0.00% */
        if (g_ui_ctx.sim_battery_bar != 0) {
            lv_bar_set_value(g_ui_ctx.sim_battery_bar, 0, LV_ANIM_OFF);
        }
        if (g_ui_ctx.sim_battery_label != 0) {
            lv_label_set_text(g_ui_ctx.sim_battery_label, "\347\224\265\351\207\217\357\274\2320.00%"); /* 电量：0.00% */
        }
        return;
    }
    ui_sim_update_progress_bar(sim_output);
    /*
     * 褰撳墠鎼哄甫閲忔寚褰撳墠妗跺墿浣欙紝涓嶆槸鍏ㄥ眬浠诲姟鍓╀綑
     */
    if ((state == APP_STATE_SIM_DONE) && (sim_output->running == 0U)) {
        (void)snprintf(summary_text,
                       sizeof(summary_text),
                       "[\344\275\234\344\270\232\346\212\245\345\221\212]\n" /* [作业报告] */
                       "\344\275\234\347\211\251\351\230\266\346\256\265   : %s-%s\n" /* 作物阶段   : %s-%s */
                       "\347\216\257\345\242\203\347\212\266\346\200\201   : %s\n" /* 环境状态   : %s */
                       "\345\226\267\346\264\222\351\235\242\347\247\257   : %lu.%02lu\345\271\263\346\226\271\347\261\263\n" /* 喷洒面积   : %lu.%02lu平方米 */
                       "\346\200\273\350\215\257\346\266\262     : %lu.%01luL\n" /* 总药液     : %lu.%01luL */
                       "\346\270\205\346\260\264/\345\206\234\350\215\257  : %lu.%01luL / %lu.%01luL\n" /* 清水/农药  : %lu.%01luL / %lu.%01luL */
                       "\346\200\273\350\210\252\347\250\213     : %lu.%03lukm\n" /* 总航程     : %lu.%03lukm */
                       "\351\227\255\347\216\257\350\267\257\345\276\204   : %u\346\256\265\n" /* 闭环路径   : %u段 */
                       "\350\241\245\346\266\262\346\254\241\346\225\260   : %u\346\254\241\n" /* 补液次数   : %u次 */
                       "\345\205\205\347\224\265\346\254\241\346\225\260   : %u\346\254\241\n" /* 充电次数   : %u次 */
                       "\347\224\265\351\207\217       : %u.%02u%%\n" /* 电量       : %u.%02u%% */
                       "\350\246\206\347\233\226\347\216\207     : %u.%02u%%", /* 覆盖率     : %u.%02u%% */
                       ui_crop_name(monitor_output->crop_type),
                       ui_growth_stage_name(monitor_output->growth_stage),
                       environment_text,
                       (unsigned long)(monitor_output->stats.total_spray_area_m2_x100 / 100U),
                       (unsigned long)(monitor_output->stats.total_spray_area_m2_x100 % 100U),
                       (unsigned long)(mix_output->summary.total_liquid_ml_x10 / 10000U),
                       (unsigned long)((mix_output->summary.total_liquid_ml_x10 % 10000U) / 1000U),
                       (unsigned long)(mix_output->summary.total_water_ml_x10 / 10000U),
                       (unsigned long)((mix_output->summary.total_water_ml_x10 % 10000U) / 1000U),
                       (unsigned long)(mix_output->summary.total_pesticide_ml_x10 / 10000U),
                       (unsigned long)((mix_output->summary.total_pesticide_ml_x10 % 10000U) / 1000U),
                       (unsigned long)(plan_output->summary.total_distance_mm / 1000000U),
                       (unsigned long)((plan_output->summary.total_distance_mm % 1000000U) / 1000U),
                       plan_output->sub_batch_count,
                       sim_output->refill_count,
                       sim_output->charge_count,
                       sim_output->battery_x100 / 100U,
                       sim_output->battery_x100 % 100U,
                       sim_output->cover_rate_x100 / 100U,
                       sim_output->cover_rate_x100 % 100U);
    } else {
        (void)snprintf(summary_text,
                       sizeof(summary_text),
                       "[\346\211\247\350\241\214\346\246\202\350\247\210]\n" /* [执行概览] */
                       "\351\227\255\347\216\257\350\267\257\345\276\204   : %u\346\256\265\n" /* 闭环路径   : %u段 */
                       "\345\275\223\345\211\215\346\220\272\345\270\246   : %lu.%01luL\n" /* 当前携带   : %lu.%01luL */
                       "\345\267\262\347\224\250\346\227\266\351\227\264   : %lu.%01lus\n" /* 已用时间   : %lu.%01lus */
                       "\345\267\262\351\243\236\350\210\252\347\250\213   : %lu.%03lukm\n" /* 已飞航程   : %lu.%03lukm */
                       "\345\267\262\345\226\267\350\210\252\347\250\213   : %lu.%03lukm\n" /* 已喷航程   : %lu.%03lukm */
                       "\350\267\257\347\272\277\350\277\233\345\272\246   : %u.%02u%%\n" /* 路线进度   : %u.%02u%% */
                       "\347\224\265\351\207\217       : %u.%02u%%\n" /* 电量       : %u.%02u%% */
                       "\350\246\206\347\233\226\347\216\207     : %u.%02u%%", /* 覆盖率     : %u.%02u%% */
                       plan_output->sub_batch_count,
                       (unsigned long)(sim_output->remain_liquid_ml_x10 / 10000U),
                       (unsigned long)((sim_output->remain_liquid_ml_x10 % 10000U) / 1000U),
                       (unsigned long)(sim_output->elapsed_time_ms / 1000U),
                       (unsigned long)((sim_output->elapsed_time_ms % 1000U) / 100U),
                       (unsigned long)(sim_output->route_distance_done_mm / 1000000U),
                       (unsigned long)((sim_output->route_distance_done_mm % 1000000U) / 1000U),
                       (unsigned long)(sim_output->spray_distance_done_mm / 1000000U),
                       (unsigned long)((sim_output->spray_distance_done_mm % 1000000U) / 1000U),
                       sim_output->progress_x100 / 100U,
                       sim_output->progress_x100 % 100U,
                       sim_output->battery_x100 / 100U,
                       sim_output->battery_x100 % 100U,
                       sim_output->cover_rate_x100 / 100U,
                       sim_output->cover_rate_x100 % 100U);
    }
    lv_label_set_text(g_ui_ctx.sim_summary_label, summary_text);
    current_index = sim_output->current_waypoint_index;
    if (sim_output->running != 0U && (sim_output->state == SIM_STATE_RETURNING_TO_HOME ||
                                      sim_output->state == SIM_STATE_REFILLING ||
                                      sim_output->state == SIM_STATE_RETURNING_TO_BREAKPOINT)) {
        const char *action_str = "\346\227\240"; /* 无 */
        if (sim_output->state == SIM_STATE_RETURNING_TO_HOME) {
            action_str = (sim_output->rth_trigger_source == 3U) ? "\344\275\216\347\224\265\350\277\224\350\210\252\345\205\205\347\224\265" : "\350\277\224\350\210\252\346\234\215\345\212\241\347\202\271"; /* 低电返航充电，返航服务点 */
        } else if (sim_output->state == SIM_STATE_REFILLING) {
            action_str = (sim_output->rth_trigger_source == 3U) ? "\346\234\215\345\212\241\347\202\271\345\205\205\347\224\265" : "\346\234\215\345\212\241\347\202\271\345\212\240\350\215\257"; /* 服务点充电，服务点加药 */
        } else {
            action_str = "\350\207\252\345\212\250\347\273\255\345\226\267\350\277\224\345\233\236\346\226\255\347\202\271"; /* 自动续喷返回断点 */
        }
        (void)snprintf(current_text,
                       sizeof(current_text),
                       "[\345\275\223\345\211\215\344\273\273\345\212\241]\n" /* [当前任务] */
                       "\345\212\250\344\275\234 : %s\n" /* 动作 : %s */
                       "\350\267\257\345\276\204 : -\n" /* 路径 : - */
                       "\346\220\272\345\270\246 : %lu.%01luL\n" /* 携带 : %lu.%01luL */
                       "\344\275\215\347\275\256 : \346\250\252\345\220\221 %ld.%01ldm  \347\272\265\345\220\221 %ld.%01ldm", /* 位置 : 横向 %ld.%01ldm  纵向 %ld.%01ldm */
                       action_str,
                       (unsigned long)(sim_output->remain_liquid_ml_x10 / 10000U),
                       (unsigned long)((sim_output->remain_liquid_ml_x10 % 10000U) / 1000U),
                       ui_mm_to_m_integer(sim_output->current_position.x_mm),
                       ui_mm_to_m_decimal(sim_output->current_position.x_mm),
                       ui_mm_to_m_integer(sim_output->current_position.y_mm),
                       ui_mm_to_m_decimal(sim_output->current_position.y_mm));
    } else if ((plan_output->waypoint_count > 0U) && (current_index < plan_output->waypoint_count)) {
        current_waypoint = &plan_output->waypoint[current_index];
        task_number_name = ui_cn_number_name(current_waypoint->sub_batch_id);
        {
            const char *prefix = ui_get_sub_batch_prefix(current_waypoint->sub_batch_id);
            char full_task_name[64];
            if (task_number_name != 0) {
                (void)snprintf(full_task_name, sizeof(full_task_name), "%s\344\273\273\345\212\241%s", prefix, task_number_name); /* %s任务%s */
            } else {
                (void)snprintf(full_task_name, sizeof(full_task_name), "%s\344\273\273\345\212\241%u", prefix, current_waypoint->sub_batch_id); /* %s任务%u */
            }
            (void)snprintf(current_text,
                           sizeof(current_text),
                           "[\345\275\223\345\211\215\344\273\273\345\212\241]\n" /* [当前任务] */
                           "\345\212\250\344\275\234 : %s\n" /* 动作 : %s */
                           "\344\273\273\345\212\241\347\274\226\345\217\267 : %s\n" /* 任务编号 : %s */
                           "\346\220\272\345\270\246 : %lu.%01luL\n" /* 携带 : %lu.%01luL */
                           "\344\275\215\347\275\256 : \346\250\252\345\220\221 %ld.%01ldm  \347\272\265\345\220\221 %ld.%01ldm", /* 位置 : 横向 %ld.%01ldm  纵向 %ld.%01ldm */
                           ui_plan_action_name(current_waypoint->action),
                           full_task_name,
                           (unsigned long)(sim_output->remain_liquid_ml_x10 / 10000U),
                           (unsigned long)((sim_output->remain_liquid_ml_x10 % 10000U) / 1000U),
                           ui_mm_to_m_integer(sim_output->current_position.x_mm),
                           ui_mm_to_m_decimal(sim_output->current_position.x_mm),
                           ui_mm_to_m_integer(sim_output->current_position.y_mm),
                           ui_mm_to_m_decimal(sim_output->current_position.y_mm));
        }
    } else {
        (void)snprintf(current_text,
                       sizeof(current_text),
                       "[\345\275\223\345\211\215\344\273\273\345\212\241]\n\346\227\240\345\217\257\346\211\247\350\241\214\350\210\252\347\202\271"); /* [当前任务]
无可执行航点 */
    }
    lv_label_set_text(g_ui_ctx.sim_current_label, current_text);
    if (sim_output->alarm_count > 0U) {
        (void)snprintf(alarm_text,
                       sizeof(alarm_text),
                       "[\346\212\245\350\255\246]\n%u\346\235\241 | \346\234\200\350\277\221:%s", /* [报警]
%u条 | 最近:%s */
                       sim_output->alarm_count,
                       ui_alarm_name(sim_output->alarm[0].code));
    } else {
        (void)snprintf(alarm_text, sizeof(alarm_text), "[\346\212\245\350\255\246]\n\346\227\240"); /* [报警]
无 */
    }
    lv_label_set_text(g_ui_ctx.sim_alarm_label, alarm_text);
    ui_sim_update_map();
}

