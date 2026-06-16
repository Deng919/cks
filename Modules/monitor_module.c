#include <string.h>

#include "monitor_module.h"
#include "app_math.h"
#include "data_table.h"

/*
 * 农田检测模块
 * 根据网格生成地块坐标、面积、状态和作业 region
 */

static uint32_t g_monitor_auto_seed;

/* 填充一份默认监测输入 */
void monitor_module_fill_default_input(monitor_input_t *input)
{
    if (input == 0) {
        return;
    }

    /* 网格默认值放在数据表里，UI 不直接写尺寸 */
    (void)memset(input, 0, sizeof(*input));
    input->grid = g_data_default_monitor_grid;
    input->environment = g_data_default_environment;
    input->mode = MONITOR_MODE_INIT_ONLY;
    input->random_seed = 20260607UL;
    input->crop_type = g_data_default_crop.crop_type;
    input->growth_stage = g_data_default_crop.growth_stage;
    input->manual_plot_id = 1U;
    input->manual_state = PLOT_STATE_HEALTHY;
    input->manual_disease_type = DISEASE_TYPE_NONE;
}

/* 生成可复现的伪随机序列 */
static uint32_t monitor_next_random(uint32_t *state)
{
    /* 不用标准库 rand，避免不同平台结果不一致 */
    *state = (*state * 1664525UL) + 1013904223UL;
    return *state;
}

/* 校验网格尺寸，保护固定大小的 plot_status 数组 */
static app_result_t monitor_validate_grid(const monitor_grid_t *grid)
{
    uint32_t plot_count;

    if (grid == 0) {
        return APP_ERR_PARAM;
    }

    if ((grid->rows == 0U) || (grid->cols == 0U) || (grid->cell_width_mm == 0U) || (grid->cell_height_mm == 0U)) {
        return APP_ERR_PARAM;
    }

    if ((grid->rows > APP_MAX_FIELD_ROWS) || (grid->cols > APP_MAX_FIELD_COLS)) {
        return APP_ERR_LIMIT;
    }

    plot_count = (uint32_t)grid->rows * (uint32_t)grid->cols;
    if (plot_count > APP_MAX_PLOT_COUNT) {
        return APP_ERR_LIMIT;
    }

    return APP_OK;
}

/* 根据网格生成地块坐标、面积和农田边界 */
static void monitor_init_plot_array(const monitor_grid_t *grid, monitor_output_t *output)
{
    uint32_t row;
    uint32_t col;
    uint32_t index;
    uint64_t area_mm2;
    uint32_t area_m2_x100;
    uint64_t field_width_mm;
    uint64_t field_height_mm;

    /* 农田尺寸以这里生成的坐标为准 */
    area_mm2 = (uint64_t)grid->cell_width_mm * (uint64_t)grid->cell_height_mm;
    area_m2_x100 = app_u32_from_u64(area_mm2 / 10000ULL);
    field_width_mm = (uint64_t)grid->cols * (uint64_t)grid->cell_width_mm;
    field_height_mm = (uint64_t)grid->rows * (uint64_t)grid->cell_height_mm;

    /* 写入矩形农田边界 */
    output->grid = *grid;
    output->field.vertex_count = 4U;
    output->field.vertex[0].x_mm = 0;
    output->field.vertex[0].y_mm = 0;
    output->field.vertex[1].x_mm = (int32_t)field_width_mm;
    output->field.vertex[1].y_mm = 0;
    output->field.vertex[2].x_mm = (int32_t)field_width_mm;
    output->field.vertex[2].y_mm = (int32_t)field_height_mm;
    output->field.vertex[3].x_mm = 0;
    output->field.vertex[3].y_mm = (int32_t)field_height_mm;
    output->field.area_m2_x100 = app_u32_from_u64((field_width_mm * field_height_mm) / 10000ULL);
    output->plot_count = (uint16_t)((uint32_t)grid->rows * (uint32_t)grid->cols);

    /* 地块按行优先排列，坐标单位 mm */
    for (row = 0U; row < grid->rows; row++) {
        for (col = 0U; col < grid->cols; col++) {
            index = (row * grid->cols) + col;
            output->plot_status[index].plot_id = (uint16_t)(index + 1U);
            output->plot_status[index].row = (uint16_t)row;
            output->plot_status[index].col = (uint16_t)col;
            output->plot_status[index].x_mm = (int32_t)((uint32_t)col * (uint32_t)grid->cell_width_mm);
            output->plot_status[index].y_mm = (int32_t)((uint32_t)row * (uint32_t)grid->cell_height_mm);
            output->plot_status[index].area_m2_x100 = area_m2_x100;
            output->plot_status[index].state = PLOT_STATE_HEALTHY;
            output->plot_status[index].disease_type = DISEASE_TYPE_NONE;
        }
    }
}

/* 按配置权重抽取病害类型 */
static disease_type_t monitor_random_disease(uint32_t *seed)
{
    uint32_t total_weight;
    uint32_t random_value;
    uint32_t acc_weight;
    uint16_t i;

    total_weight = 0U;
    for (i = 0U; i < APP_MAX_DISEASE_TYPE_COUNT; i++) {
        total_weight += g_data_monitor_auto_profile.disease_weight[i];
    }

    if (total_weight == 0U) {
        return DISEASE_TYPE_BLIGHT;
    }

    random_value = monitor_next_random(seed) % total_weight;
    acc_weight = 0U;
    for (i = 0U; i < APP_MAX_DISEASE_TYPE_COUNT; i++) {
        acc_weight += g_data_monitor_auto_profile.disease_weight[i];
        if (random_value < acc_weight) {
            /* 病害枚举从 1 开始，权重数组下标从 0 开始 */
            return (disease_type_t)(i + 1U);
        }
    }

    return DISEASE_TYPE_MILDEW;
}

/* 自动检测模式下生成地块状态 */
static void monitor_apply_auto_detect(monitor_output_t *output, uint32_t random_seed)
{
    uint32_t i;
    uint32_t value;
    uint32_t seed;
    uint32_t healthy_thresh;
    uint32_t water_thresh;
    uint32_t healthy_span;
    uint32_t water_span;

    /* 两个阈值把 0~99 分成健康、缺水、病害三段 */
    /* 没有外部种子时沿用内部随机序列 */
    if (random_seed == 0U) {
        g_monitor_auto_seed = monitor_next_random(&g_monitor_auto_seed);
        seed = g_monitor_auto_seed ^ 0xABCDEF12UL;
    } else {
        seed = random_seed;
    }

    if (seed == 0U) {
        seed = 1U;
    }

    /* 随机生成健康阈值 */
    if (g_data_monitor_auto_profile.healthy_max_pct < g_data_monitor_auto_profile.healthy_min_pct) {
        healthy_thresh = g_data_monitor_auto_profile.healthy_min_pct;
    } else {
        healthy_span = (uint32_t)g_data_monitor_auto_profile.healthy_max_pct -
                       (uint32_t)g_data_monitor_auto_profile.healthy_min_pct + 1U;
        healthy_thresh = (uint32_t)g_data_monitor_auto_profile.healthy_min_pct +
                          (monitor_next_random(&seed) % healthy_span);
    }

    /* 缺水阈值接在健康阈值后面 */
    if (g_data_monitor_auto_profile.water_max_pct < g_data_monitor_auto_profile.water_min_pct) {
        water_span = 0U;
    } else {
        water_span = (uint32_t)g_data_monitor_auto_profile.water_max_pct -
                     (uint32_t)g_data_monitor_auto_profile.water_min_pct + 1U;
    }

    water_thresh = healthy_thresh + (uint32_t)g_data_monitor_auto_profile.water_min_pct;
    if (water_span > 0U) {
        water_thresh += monitor_next_random(&seed) % water_span;
    }
    if (water_thresh > 100U) {
        water_thresh = 100U;
    }

    /* 按阈值给每个地块分配状态 */
    for (i = 0U; i < output->plot_count; i++) {
        value = monitor_next_random(&seed) % 100U;

        if (value < healthy_thresh) {
            output->plot_status[i].state = PLOT_STATE_HEALTHY;
            output->plot_status[i].disease_type = DISEASE_TYPE_NONE;
        } else if (value < water_thresh) {
            output->plot_status[i].state = PLOT_STATE_WATER_DEFICIT;
            output->plot_status[i].disease_type = DISEASE_TYPE_NONE;
        } else {
            output->plot_status[i].state = PLOT_STATE_DISEASE;
            output->plot_status[i].disease_type = monitor_random_disease(&seed);
        }
    }
}

/* 把手动选择的单块地状态写回检测结果 */
static app_result_t monitor_apply_manual_set(monitor_output_t *output, const monitor_input_t *input)
{
    uint16_t index;

    /*
     * 手动设置只允许三种合法组合：
     * - 健康：不能带病害类型；
     * - 缺水：不能带病害类型；
     * - 病害：必须指定具体病害类型
     *
     * 防止出现“健康但带病害类型”这类下游不好处理的数据
     */
    if ((output == 0) || (input == 0)) {
        return APP_ERR_PARAM;
    }
    if ((input->manual_plot_id == 0U) || (input->manual_plot_id > output->plot_count)) {
        return APP_ERR_PARAM;
    }
    if ((input->manual_state == PLOT_STATE_HEALTHY) && (input->manual_disease_type != DISEASE_TYPE_NONE)) {
        return APP_ERR_PARAM;
    }
    if ((input->manual_state == PLOT_STATE_WATER_DEFICIT) && (input->manual_disease_type != DISEASE_TYPE_NONE)) {
        return APP_ERR_PARAM;
    }
    if ((input->manual_state == PLOT_STATE_DISEASE) && (input->manual_disease_type == DISEASE_TYPE_NONE)) {
        return APP_ERR_PARAM;
    }

    index = (uint16_t)(input->manual_plot_id - 1U);
    /* plot_id 给 UI plot_id从 1 开始显示，数组下标index从 0 开始 */
    output->plot_status[index].state = input->manual_state;
    output->plot_status[index].disease_type = input->manual_disease_type;

    return APP_OK;
}

/* 汇总地块数量和喷洒面积 */
static void monitor_collect_stats(const monitor_output_t *output, monitor_stats_t *stats)
{
    uint16_t i;
    uint16_t disease_index;

    (void)memset(stats, 0, sizeof(*stats));
    for (i = 0U; i < output->plot_count; i++) {
        switch (output->plot_status[i].state) {
        case PLOT_STATE_HEALTHY:
            /* 健康地块不计入喷洒面积 */
            stats->healthy_plot_count++;
            break;
        case PLOT_STATE_WATER_DEFICIT:
            stats->water_deficit_plot_count++;
            stats->total_spray_area_m2_x100 += output->plot_status[i].area_m2_x100;
            break;
        case PLOT_STATE_DISEASE:
            disease_index = (uint16_t)output->plot_status[i].disease_type;
            if ((disease_index > 0U) && (disease_index <= APP_MAX_DISEASE_TYPE_COUNT)) {
                /* disease_index 转成数组下标后再累加对应病害 */
                disease_index--;
                stats->disease_plot_count[disease_index]++;
                stats->total_spray_area_m2_x100 += output->plot_status[i].area_m2_x100;
            }
            break;
        default:
            break;
        }
    }
}

static const crop_rule_t *monitor_find_crop_rule(uint8_t crop_type, uint8_t growth_stage)
{
    uint16_t i;

    for (i = 0U; i < g_data_crop_rule_count; i++) {
        if ((g_data_crop_rule_table[i].crop_type == crop_type) &&
            (g_data_crop_rule_table[i].growth_stage == growth_stage)) {
            return &g_data_crop_rule_table[i];
        }
    }

    return &g_data_default_crop;
}

static void monitor_evaluate_environment(const environment_input_t *input, const crop_rule_t *crop_rule, environment_status_t *status)
{
    uint16_t factor_x100;

    if ((input == 0) || (crop_rule == 0) || (status == 0)) {
        return;
    }

    (void)memset(status, 0, sizeof(*status));
    status->input = *input;

    status->temperature_ok = ((input->temperature_x10 >= crop_rule->temp_low_x10) &&
                              (input->temperature_x10 <= crop_rule->temp_high_x10)) ? 1U : 0U;
    status->humidity_ok = ((input->humidity_x10 >= crop_rule->humidity_low_x10) &&
                           (input->humidity_x10 <= crop_rule->humidity_high_x10)) ? 1U : 0U;
    status->light_ok = ((input->light_lux >= crop_rule->light_low_lux) &&
                        (input->light_lux <= crop_rule->light_high_lux)) ? 1U : 0U;
    status->wind_ok = (input->wind_speed_x10 <= crop_rule->max_wind_x10) ? 1U : 0U;
    status->suitable = ((status->temperature_ok != 0U) &&
                        (status->humidity_ok != 0U) &&
                        (status->light_ok != 0U) &&
                        (status->wind_ok != 0U)) ? 1U : 0U;

    factor_x100 = 100U;
    if (input->temperature_x10 > crop_rule->temp_high_x10) {
        factor_x100 = (uint16_t)(factor_x100 + 10U);
    }
    if (input->humidity_x10 < crop_rule->humidity_low_x10) {
        factor_x100 = (uint16_t)(factor_x100 + 10U);
    }
    if (input->light_lux > crop_rule->light_high_lux) {
        factor_x100 = (uint16_t)(factor_x100 + 10U);
    }
    if (factor_x100 > APP_ENV_FACTOR_MAX_X100) {
        factor_x100 = APP_ENV_FACTOR_MAX_X100;
    }
    status->environment_factor_x100 = factor_x100;
}

static uint16_t monitor_calc_base_severity(uint16_t affected_count, uint16_t plot_count, uint16_t light_pct, uint16_t medium_pct)
{
    uint32_t affected_pct_x100;

    if ((affected_count == 0U) || (plot_count == 0U)) {
        return APP_SEVERITY_MEDIUM_X100;
    }

    affected_pct_x100 = ((uint32_t)affected_count * 10000UL) / (uint32_t)plot_count;
    if (affected_pct_x100 < ((uint32_t)light_pct * 100UL)) {
        return APP_SEVERITY_LIGHT_X100;
    }
    if (affected_pct_x100 <= ((uint32_t)medium_pct * 100UL)) {
        return APP_SEVERITY_MEDIUM_X100;
    }

    return APP_SEVERITY_HEAVY_X100;
}

static uint16_t monitor_apply_environment_factor(uint16_t base_severity_x100, uint16_t environment_factor_x100)
{
    uint32_t severity;

    severity = ((uint32_t)base_severity_x100 * (uint32_t)environment_factor_x100) / 100U;
    if (severity < APP_SEVERITY_MIN_X100) {
        severity = APP_SEVERITY_MIN_X100;
    }
    if (severity > APP_SEVERITY_MAX_X100) {
        severity = APP_SEVERITY_MAX_X100;
    }

    return (uint16_t)severity;
}

/* 根据统计结果生成配药用的作业 region */
static void monitor_build_region_list(const monitor_stats_t *stats, monitor_output_t *output)
{
    uint16_t disease_index;

    /*
     * 缺水地块生成清水 region，病害地块按病害类型生成农药 region
     */
    output->region_count = 0U;

    if (stats->water_deficit_plot_count > 0U) {
        output->region[output->region_count].spray_content = SPRAY_CONTENT_WATER;
        output->region[output->region_count].disease_type = DISEASE_TYPE_NONE;
        output->region[output->region_count].severity_x100 =
            monitor_apply_environment_factor(
                monitor_calc_base_severity(stats->water_deficit_plot_count, output->plot_count, 10U, 20U),
                output->environment_status.environment_factor_x100);
        output->region_count++;
    }

    for (disease_index = 0U; disease_index < APP_MAX_DISEASE_TYPE_COUNT; disease_index++) {
        if (stats->disease_plot_count[disease_index] == 0U) {
            continue;
        }
        if (output->region_count >= APP_MAX_MONITOR_REGION_COUNT) {
            break;
        }
        output->region[output->region_count].spray_content = SPRAY_CONTENT_PESTICIDE;
        output->region[output->region_count].disease_type = (disease_type_t)(disease_index + 1U);
        output->region[output->region_count].severity_x100 =
            monitor_apply_environment_factor(
                monitor_calc_base_severity(stats->disease_plot_count[disease_index], output->plot_count, 8U, 16U),
                output->environment_status.environment_factor_x100);
        output->region_count++;
    }
}

/* 初始化自动检测备用随机种子 */
void monitor_module_init(void)
{
    /* input->random_seed 为 0 时才会用这个备用种子 */
    g_monitor_auto_seed = 0x13572468UL;
}

/* 在已有检测输出上改一块地，并重算统计和 region */
app_result_t monitor_module_apply_manual(const monitor_input_t *input, monitor_output_t *output)
{
    app_result_t result;

    if ((input == 0) || (output == 0)) {
        return APP_ERR_PARAM;
    }
    if (input->mode != MONITOR_MODE_MANUAL_SET) {
        return APP_ERR_PARAM;
    }

    result = monitor_validate_grid(&output->grid);
    if (result != APP_OK) {
        return result;
    }

    result = monitor_apply_manual_set(output, input);
    if (result != APP_OK) {
        return result;
    }

    if (output->environment_status.environment_factor_x100 == 0U) {
        output->crop_type = input->crop_type;
        output->growth_stage = input->growth_stage;
        monitor_evaluate_environment(&input->environment,
                                     monitor_find_crop_rule(input->crop_type, input->growth_stage),
                                     &output->environment_status);
    }
    monitor_collect_stats(output, &output->stats);
    monitor_build_region_list(&output->stats, output);

    return APP_OK;
}

/* 执行一次完整检测 */
app_result_t monitor_module_run(const monitor_input_t *input, monitor_output_t *output)
{
    app_result_t result;

    if ((input == 0) || (output == 0)) {
        return APP_ERR_PARAM;
    }

    result = monitor_validate_grid(&input->grid);
    if (result != APP_OK) {
        return result;
    }

    /* 完整检测时重建输出，旧地块状态不保留 */
    (void)memset(output, 0, sizeof(*output));
    monitor_init_plot_array(&input->grid, output);
    output->crop_type = input->crop_type;
    output->growth_stage = input->growth_stage;

    if (input->mode == MONITOR_MODE_AUTO_DETECT) {
        monitor_apply_auto_detect(output, input->random_seed);
    } else if (input->mode == MONITOR_MODE_MANUAL_SET) {
        result = monitor_apply_manual_set(output, input);
        if (result != APP_OK) {
            return result;
        }
    }

    monitor_evaluate_environment(&input->environment,
                                 monitor_find_crop_rule(input->crop_type, input->growth_stage),
                                 &output->environment_status);
    monitor_collect_stats(output, &output->stats);
    monitor_build_region_list(&output->stats, output);

    return APP_OK;
}
