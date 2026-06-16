#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mix_module.h"
#include "app_math.h"
#include "data_table.h"

/*
 * 配药模块
 * main_batch 表示作业类型，sub_batch 表示一桶任务，chunk 表示桶内地块或切片
 */

static uint16_t g_mix_region_plot_order[APP_MAX_PLOT_COUNT];
static uint16_t g_mix_candidate[APP_MAX_PLOT_COUNT];
static uint8_t g_mix_candidate_used[APP_MAX_PLOT_COUNT];

/* 根据喷洒内容和病害类型查配方 */
static const mix_recipe_t *mix_find_recipe(spray_content_t spray_content, disease_type_t disease_type)
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

/* 追加非致命告警文本 */
static void mix_append_warning(mix_output_t *output, const char *text)
{
    size_t current_len;
    size_t remain_len;

    /* 固定缓冲区，追加前先卡条数和剩余空间 */
    if ((output == 0) || (text == 0) || (output->summary.warning_count >= 9U)) {
        return;
    }

    current_len = strlen(output->summary.warning_text);
    remain_len = sizeof(output->summary.warning_text) - current_len;
    if (remain_len <= 2U) {
        return;
    }

    if (current_len > 0U) {
        (void)snprintf(&output->summary.warning_text[current_len], remain_len, "\n%s", text);
    } else {
        (void)snprintf(output->summary.warning_text, sizeof(output->summary.warning_text), "%s", text);
    }
    output->summary.warning_count++;
}

/* 估算两个地块中心的曼哈顿距离 */
static uint32_t mix_plot_distance(const plot_status_t *from, const plot_status_t *to, const monitor_grid_t *grid)
{
    int32_t from_x;
    int32_t from_y;
    int32_t to_x;
    int32_t to_y;

    /* 这里只服务排序，最终航线由 plan_module 生成 */
    if ((from == 0) || (to == 0) || (grid == 0)) {
        return 0xFFFFFFFFU;//非法对象会被当成非常远，基本不会被选中
    }

    /*第一个坐标取左上角，计算出来的是中心点*/
    from_x = from->x_mm + (int32_t)(grid->cell_width_mm / 2U);
    from_y = from->y_mm + (int32_t)(grid->cell_height_mm / 2U);
    to_x = to->x_mm + (int32_t)(grid->cell_width_mm / 2U);
    to_y = to->y_mm + (int32_t)(grid->cell_height_mm / 2U);

    return app_abs_u32(to_x - from_x) + app_abs_u32(to_y - from_y);
}

/* 筛出当前 region 的地块，并排成一个大致连续的顺序 */
static uint16_t mix_collect_region_plot_order(const monitor_output_t *monitor,
                                              const monitor_region_t *region,
                                              uint16_t *plot_order,
                                              uint16_t max_count)
{
    uint16_t *candidate;
    uint8_t *used;
    uint16_t candidate_count;
    uint16_t output_count;
    uint16_t i;
    uint16_t current_index;

    /* 这个顺序不是航线，只是给后续拆桶一个更顺的地块序列 */
    if ((monitor == 0) || (region == 0) || (plot_order == 0) || (max_count == 0U)) {
        return 0U;
    }
    if (max_count > APP_MAX_PLOT_COUNT) {
        max_count = APP_MAX_PLOT_COUNT;
    }

    candidate = g_mix_candidate;
    used = g_mix_candidate_used;
    (void)memset(used, 0, sizeof(g_mix_candidate_used));
    candidate_count = 0U;
    /* 先按区域条件收集候选地块 */
    for (i = 0U; (i < monitor->plot_count) && (candidate_count < max_count); i++) {
        const plot_status_t *plot;

        plot = &monitor->plot_status[i];
        if ((region->spray_content == SPRAY_CONTENT_WATER) && (plot->state != PLOT_STATE_WATER_DEFICIT)) {
            continue;
        }
        if ((region->spray_content == SPRAY_CONTENT_PESTICIDE) &&
            !((plot->state == PLOT_STATE_DISEASE) && (plot->disease_type == region->disease_type))) {
            continue;
        }
        candidate[candidate_count] = i;
        candidate_count++;
    }

    /*候选地块数量小于等于 1 的处理*/
    if (candidate_count <= 1U) {
        if (candidate_count == 1U) {
            plot_order[0] = candidate[0];
        }
        return candidate_count;
    }

    {
        uint16_t start_candidate;
        uint32_t best_score;

        /* 选择起始地块，从靠近原点的地块开始，减少排序随机感 */
        start_candidate = 0U;
        best_score = 0xFFFFFFFFU;
        for (i = 0U; i < candidate_count; i++) {
            const plot_status_t *plot;
            uint32_t score;

            plot = &monitor->plot_status[candidate[i]];
            score = app_abs_u32(plot->x_mm) + app_abs_u32(plot->y_mm);
            if (score < best_score) {
                best_score = score;
                start_candidate = i;
            }
        }
        output_count = 1U;
        current_index = candidate[start_candidate];
        plot_order[0] = current_index;
        used[start_candidate] = 1U;
    }
    /* 对剩下的地块进行最近邻排序 */
    while (output_count < candidate_count) {
        uint16_t best_candidate;
        uint32_t best_distance;

        best_candidate = 0U;
        best_distance = 0xFFFFFFFFU;
        for (i = 0U; i < candidate_count; i++) {
            uint32_t distance;

            if (used[i] != 0U) {
                continue;
            }
            distance = mix_plot_distance(&monitor->plot_status[current_index],
                                         &monitor->plot_status[candidate[i]],
                                         &monitor->grid);
            if (distance < best_distance) {
                best_distance = distance;
                best_candidate = i;
            }
        }
        used[best_candidate] = 1U;
        current_index = candidate[best_candidate];
        plot_order[output_count] = current_index;
        output_count++;
    }

    return candidate_count;
}

/* 按面积、单位用量和严重度计算液量，结果单位 ml_x10 */
static uint32_t mix_calc_liquid_ml_x10(uint32_t dose_ml_per_m2_x100,
                                       uint32_t area_m2_x100,
                                       uint16_t severity_x100)
{
    uint64_t value;

    /* 全程用整数放大单位，避免浮点 */
    value = (uint64_t)dose_ml_per_m2_x100 * (uint64_t)area_m2_x100 * (uint64_t)severity_x100;
    value /= 100000ULL;

    return app_u32_from_u64(value);
}

/* 把一个完整地块或切片加入桶任务 */
static app_result_t mix_append_chunk(mix_sub_batch_t *sub_batch,
                                     uint16_t plot_id,
                                     int32_t min_x_mm,
                                     int32_t max_x_mm,
                                     int32_t min_y_mm,
                                     int32_t max_y_mm)
{
    mix_chunk_t *chunk;

    if (sub_batch == 0) {
        return APP_ERR_PARAM;
    }
    if (sub_batch->chunk_count >= APP_MAX_MIX_CHUNK_PER_SUB_BATCH) {
        return APP_ERR_LIMIT;
    }

    chunk = &sub_batch->chunk[sub_batch->chunk_count];
    chunk->plot_id = plot_id;
    chunk->min_x_mm = min_x_mm;
    chunk->max_x_mm = max_x_mm;
    chunk->min_y_mm = min_y_mm;
    chunk->max_y_mm = max_y_mm;
    sub_batch->chunk_count++;

    return APP_OK;
}

/* 当前无内部状态，保留模块初始化入口 */
void mix_module_init(void)
{
}

/* 把监测 region 转成主批次、桶任务和液量统计 */
app_result_t mix_module_run(const monitor_output_t *monitor, const mix_input_t *input, mix_output_t *output)
{
    uint16_t region_index;
    uint16_t main_index;
    uint16_t sub_index;

    /* region 决定喷什么，plot_status 决定哪些地块要喷 */
    if ((monitor == 0) || (input == 0) || (output == 0)) {
        return APP_ERR_PARAM;
    }

    if (input->tank_capacity_ml_x10 == 0U) {
        return APP_ERR_PARAM;
    }

    /*把整个输出结构体清零*/
    (void)memset(output, 0, sizeof(*output));
    if (monitor->region_count == 0U) {
        return APP_ERR_STATE;
    }

    main_index = 0U;
    sub_index = 0U;

    /* 每个 region 生成一个主批次，再按容量拆成桶任务 */
    for (region_index = 0U; region_index < monitor->region_count; region_index++) {
        const monitor_region_t *region;
        const mix_recipe_t *recipe;
        mix_main_batch_t *main_batch;
        uint16_t ordered_plot_count;
        uint16_t plot_index;
        uint32_t current_bucket_used_ml_x10;

        /* 跨 main_batch 表示换液，同一 main_batch 内只是续液 */
        region = &monitor->region[region_index];
        recipe = mix_find_recipe(region->spray_content, region->disease_type);
        if (recipe == 0) {
            mix_append_warning(output, "缺少配药配方映射");
            continue;
        }

        if (main_index >= APP_MAX_MIX_MAIN_BATCH_COUNT) {
            return APP_ERR_LIMIT;
        }

        main_batch = &output->main_batch[main_index];
        main_batch->main_batch_id = (uint16_t)(main_index + 1U);
        main_batch->spray_content = region->spray_content;
        main_batch->disease_type = region->disease_type;
        main_batch->severity_x100 = region->severity_x100;
        main_batch->sub_batch_start = sub_index;
        current_bucket_used_ml_x10 = 0U;
        ordered_plot_count = mix_collect_region_plot_order(monitor,region,g_mix_region_plot_order,APP_MAX_PLOT_COUNT);

        /*遍历当前 region 的地块*/
        for (plot_index = 0U; plot_index < ordered_plot_count; plot_index++) {
            const plot_status_t *plot;
            uint32_t plot_water_ml_x10;
            uint32_t plot_pesticide_ml_x10;
            uint32_t plot_liquid_ml_x10;
            mix_sub_batch_t *sub_batch;
            int32_t plot_max_x_mm;
            int32_t plot_max_y_mm;

            /* 每块地先算完整液量，再看当前桶能装多少 */
            plot = &monitor->plot_status[g_mix_region_plot_order[plot_index]];
            plot_water_ml_x10 = mix_calc_liquid_ml_x10(recipe->water_ml_per_m2_x100,plot->area_m2_x100,region->severity_x100);
            plot_pesticide_ml_x10 = mix_calc_liquid_ml_x10(recipe->pesticide_ml_per_m2_x100,plot->area_m2_x100,region->severity_x100);
            plot_liquid_ml_x10 = app_u32_from_u64((uint64_t)plot_water_ml_x10 +(uint64_t)plot_pesticide_ml_x10);
            
            /*兜底处理，避免太小被截断*/
            if (plot_liquid_ml_x10 == 0U) {
                plot_liquid_ml_x10 = 10U;
                plot_water_ml_x10 = 10U;
            }
            plot_max_x_mm = plot->x_mm + (int32_t)monitor->grid.cell_width_mm;
            plot_max_y_mm = plot->y_mm + (int32_t)monitor->grid.cell_height_mm;

            /* 地块间距不触发返航，容量不足才拆桶 */
            if (plot_liquid_ml_x10 > input->tank_capacity_ml_x10) {
                mix_append_warning(output, "单个地块已拆分到多个桶任务");
            }

            {
                uint32_t remaining_area_m2_x100;
                uint32_t remaining_liquid_ml_x10;
                uint32_t remaining_water_ml_x10;
                uint32_t remaining_pesticide_ml_x10;
                int32_t remaining_min_x_mm;/*当前地块还没切分部分的左边界 X 坐标*/

                /*如果后面发生切片，每次切完后都会把它更新成上一片的右边界*/
                remaining_area_m2_x100 = plot->area_m2_x100;
                remaining_liquid_ml_x10 = plot_liquid_ml_x10;
                remaining_water_ml_x10 = plot_water_ml_x10;
                remaining_pesticide_ml_x10 = plot_pesticide_ml_x10;
                remaining_min_x_mm = plot->x_mm;

                /* 按当前桶剩余容量切片，减少半空桶 */
                while (remaining_liquid_ml_x10 > 0U) {
                    uint32_t bucket_remaining_ml_x10;
                    uint32_t chunk_area_m2_x100;
                    uint32_t chunk_liquid_ml_x10;
                    uint32_t chunk_water_ml_x10;
                    uint32_t chunk_pesticide_ml_x10;
                    int32_t chunk_min_x_mm;
                    int32_t chunk_max_x_mm;
                    app_result_t chunk_result;

                    /* 当前桶为空时新建 sub_batch */
                    if (current_bucket_used_ml_x10 == 0U) {
                        if (main_batch->sub_batch_count >= APP_MAX_MIX_SUB_BATCH_PER_MAIN) {
                            return APP_ERR_LIMIT;
                        }
                        if (sub_index >= APP_MAX_MIX_SUB_BATCH_COUNT) {
                            mix_append_warning(output, "配药桶任务数量达到上限");
                            return APP_ERR_LIMIT;
                        }

                        /*一个是主批次内部统计，一个是全局汇总统计*/
                        main_batch->bucket_count++;
                        output->summary.total_bucket_count++;

                        /* 先用极值初始化包围范围，后面随 chunk 扩展 */
                        sub_batch = &output->sub_batch[sub_index];
                        (void)memset(sub_batch, 0, sizeof(*sub_batch));
                        sub_batch->sub_batch_id = (uint16_t)(sub_index + 1U);
                        sub_batch->spray_content = region->spray_content;
                        sub_batch->disease_type = region->disease_type;
                        sub_batch->severity_x100 = region->severity_x100;
                        sub_batch->min_x_mm = 2147483647L;/*初始化桶任务边界为很大的数，遇到小的会更新*/
                        sub_batch->max_x_mm = -2147483647L;
                        sub_batch->min_y_mm = 2147483647L;
                        sub_batch->max_y_mm = -2147483647L;
                        main_batch->sub_batch_count++;
                        sub_index++;
                    }

                    /* 本次 chunk 最多占用当前桶剩余容量 */
                    bucket_remaining_ml_x10 = input->tank_capacity_ml_x10 - current_bucket_used_ml_x10;

                    chunk_liquid_ml_x10 = remaining_liquid_ml_x10;
                    if (chunk_liquid_ml_x10 > bucket_remaining_ml_x10) {
                        chunk_liquid_ml_x10 = bucket_remaining_ml_x10;
                    }

                    /* 切片按同一比例拆面积、水量和药量 */
                    if (chunk_liquid_ml_x10 == remaining_liquid_ml_x10) {
                        chunk_area_m2_x100 = remaining_area_m2_x100;
                        chunk_water_ml_x10 = remaining_water_ml_x10;
                        chunk_pesticide_ml_x10 = remaining_pesticide_ml_x10;
                    } else {
                        chunk_area_m2_x100 = app_u32_from_u64(((uint64_t)remaining_area_m2_x100 *
                                                               (uint64_t)chunk_liquid_ml_x10) /
                                                              (uint64_t)remaining_liquid_ml_x10);
                        if ((chunk_area_m2_x100 == 0U) && (remaining_area_m2_x100 > 0U)) {
                            chunk_area_m2_x100 = 1U;
                        }
                        if (chunk_area_m2_x100 > remaining_area_m2_x100) {
                            chunk_area_m2_x100 = remaining_area_m2_x100;
                        }
                        /* 下面按比例拆水和药 */
                        chunk_water_ml_x10 = app_u32_from_u64(((uint64_t)remaining_water_ml_x10 *
                                                               (uint64_t)chunk_liquid_ml_x10) /
                                                              (uint64_t)remaining_liquid_ml_x10);
                        if (chunk_water_ml_x10 > remaining_water_ml_x10) {
                            chunk_water_ml_x10 = remaining_water_ml_x10;
                        }
                        chunk_pesticide_ml_x10 = chunk_liquid_ml_x10 - chunk_water_ml_x10;
                        if (chunk_pesticide_ml_x10 > remaining_pesticide_ml_x10) {
                            chunk_pesticide_ml_x10 = remaining_pesticide_ml_x10;
                            chunk_water_ml_x10 = chunk_liquid_ml_x10 - chunk_pesticide_ml_x10;
                        }
                    }

                    /*计算 chunk 的 X 边界*/
                    chunk_min_x_mm = remaining_min_x_mm;
                    if (chunk_area_m2_x100 >= remaining_area_m2_x100) {
                        chunk_max_x_mm = plot_max_x_mm;
                    } else {
                        int32_t remaining_width_mm;
                        int32_t chunk_width_mm;

                        remaining_width_mm = plot_max_x_mm - remaining_min_x_mm;
                        chunk_width_mm = (int32_t)(((uint64_t)(uint32_t)remaining_width_mm *
                                                    (uint64_t)chunk_area_m2_x100) /
                                                    (uint64_t)remaining_area_m2_x100);
                        if (chunk_width_mm <= 0) {
                            chunk_width_mm = 1;
                        }
                        if (chunk_width_mm > remaining_width_mm) {
                            chunk_width_mm = remaining_width_mm;
                        }
                        chunk_max_x_mm = chunk_min_x_mm + chunk_width_mm;
                    }

                    /* 沿 X 方向切，保证每个 chunk 仍是矩形 */
                    /* 同步更新当前桶的包围盒和液量 */
                    sub_batch = &output->sub_batch[sub_index - 1U];
                    sub_batch->liquid_ml_x10 += chunk_liquid_ml_x10;
                    chunk_result = mix_append_chunk(sub_batch,
                                                    plot->plot_id,
                                                    chunk_min_x_mm,
                                                    chunk_max_x_mm,
                                                    plot->y_mm,
                                                    plot_max_y_mm);
                    if (chunk_result != APP_OK) {
                        return chunk_result;
                    }
                    if (chunk_min_x_mm < sub_batch->min_x_mm) {
                        sub_batch->min_x_mm = chunk_min_x_mm;
                    }
                    if (chunk_max_x_mm > sub_batch->max_x_mm) {
                        sub_batch->max_x_mm = chunk_max_x_mm;
                    }
                    if (plot->y_mm < sub_batch->min_y_mm) {
                        sub_batch->min_y_mm = plot->y_mm;
                    }
                    if (plot_max_y_mm > sub_batch->max_y_mm) {
                        sub_batch->max_y_mm = plot_max_y_mm;
                    }


                    /* 扣掉本次切片，下一轮继续处理同一地块剩余部分 */
                    remaining_area_m2_x100 -= chunk_area_m2_x100;
                    remaining_liquid_ml_x10 -= chunk_liquid_ml_x10;
                    remaining_water_ml_x10 -= chunk_water_ml_x10;
                    remaining_pesticide_ml_x10 -= chunk_pesticide_ml_x10;
                    remaining_min_x_mm = chunk_max_x_mm;

                    /* 装满后置零，下一轮会自动开新桶 */
                    current_bucket_used_ml_x10 += chunk_liquid_ml_x10;
                    if (current_bucket_used_ml_x10 >= input->tank_capacity_ml_x10) {
                        current_bucket_used_ml_x10 = 0U;
                    }
                }
            }


            /* 汇总按整块地计算，切片只影响桶任务拆分*/
            output->summary.total_liquid_ml_x10 += plot_liquid_ml_x10;
            output->summary.total_water_ml_x10 += plot_water_ml_x10;
            output->summary.total_pesticide_ml_x10 += plot_pesticide_ml_x10;
        }

        if (main_batch->sub_batch_count > 0U) {
            uint16_t local_index;


            /* sub_batch 建完后再补中心点 */
            for (local_index = 0U; local_index < main_batch->sub_batch_count; local_index++) {
                mix_sub_batch_t *sub_batch;

                sub_batch = &output->sub_batch[main_batch->sub_batch_start + local_index];
                sub_batch->center_x_mm = (sub_batch->min_x_mm + sub_batch->max_x_mm) / 2;
                sub_batch->center_y_mm = (sub_batch->min_y_mm + sub_batch->max_y_mm) / 2;
            }

            main_index++;
        }
    }

    output->main_batch_count = main_index;
    output->sub_batch_count = sub_index;
    if (output->main_batch_count == 0U && monitor->region_count > 0U) {
        return APP_ERR_STATE;
    }

    return APP_OK;
}
