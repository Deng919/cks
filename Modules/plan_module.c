#include <string.h>

#include "plan_module.h"
#include "app_math.h"

/*
 * 路径规划模块
 * 把每桶喷洒块转换成从服务点出发并返回的闭环航点
 */

typedef struct
{
    /* 规划只需要矩形 min/max 边界 */
    int32_t min_x;
    int32_t max_x;
    int32_t min_y;
    int32_t max_y;
} plan_field_bounds_t;

typedef struct
{
    /* 模块内部使用的临时喷洒矩形 */
    int32_t min_x_mm;
    int32_t max_x_mm;
    int32_t min_y_mm;
    int32_t max_y_mm;
} plan_spray_block_t;

/* 规划内部统一使用的距离计算 */
static uint32_t plan_distance_mm(const app_point_t *from, const app_point_t *to)
{
    return app_point_distance_mm(from, to);
}

/* 从农田顶点提取矩形边界 */
static app_result_t plan_get_field_bounds(const app_field_t *field, plan_field_bounds_t *bounds)
{
    uint16_t i;

    if ((field == 0) || (bounds == 0) || (field->vertex_count < 4U)) {
        return APP_ERR_PARAM;
    }

    bounds->min_x = field->vertex[0].x_mm;
    bounds->max_x = field->vertex[0].x_mm;
    bounds->min_y = field->vertex[0].y_mm;
    bounds->max_y = field->vertex[0].y_mm;

    for (i = 1U; i < field->vertex_count; i++) {
        if (field->vertex[i].x_mm < bounds->min_x) {
            bounds->min_x = field->vertex[i].x_mm;
        }
        if (field->vertex[i].x_mm > bounds->max_x) {
            bounds->max_x = field->vertex[i].x_mm;
        }
        if (field->vertex[i].y_mm < bounds->min_y) {
            bounds->min_y = field->vertex[i].y_mm;
        }
        if (field->vertex[i].y_mm > bounds->max_y) {
            bounds->max_y = field->vertex[i].y_mm;
        }
    }

    /*校验边界有效性*/
    if ((bounds->min_x >= bounds->max_x) || (bounds->min_y >= bounds->max_y)) {
        return APP_ERR_PARAM;
    }

    return APP_OK;
}

/* 计算点到矩形边界的最短距离 */
static uint32_t plan_distance_point_to_rect(const app_point_t *point, const plan_field_bounds_t *bounds)
{
    uint32_t dx;
    uint32_t dy;

    /* 点在矩形内时距离为 0 */
    dx = 0U;
    dy = 0U;

    if (point->x_mm < bounds->min_x) {
        dx = app_abs_u32(bounds->min_x - point->x_mm);
    } else if (point->x_mm > bounds->max_x) {
        dx = app_abs_u32(point->x_mm - bounds->max_x);
    }

    if (point->y_mm < bounds->min_y) {
        dy = app_abs_u32(bounds->min_y - point->y_mm);
    } else if (point->y_mm > bounds->max_y) {
        dy = app_abs_u32(point->y_mm - bounds->max_y);
    }

    if ((dx == 0U) && (dy == 0U)) {
        return 0U;
    }

    return app_isqrt_u64(((uint64_t)dx * (uint64_t)dx) + ((uint64_t)dy * (uint64_t)dy));
}

/* 根据喷幅和重叠率计算 S 型航线行距 */
static uint16_t plan_calc_lane_pitch(uint16_t spray_width_mm, uint16_t overlap_rate_x100)
{
    uint32_t pitch;

    /*
     * 最小保留 1mm，防止重叠率过高导致循环无法推进
     */
    if (spray_width_mm == 0U) {
        return 0U;
    }

    if (overlap_rate_x100 >= 10000U) {
        overlap_rate_x100 = 9900U;
    }

    pitch = ((uint32_t)spray_width_mm * (10000U - overlap_rate_x100)) / 10000U;
    if (pitch == 0U) {
        pitch = 1U;
    }

    return (uint16_t)pitch;
}

/* 计算一个矩形需要多少条横向喷洒线 */
static uint32_t plan_calc_s_lane_count(int32_t min_y, int32_t max_y, uint16_t lane_pitch_mm)
{
    uint32_t height_mm;
    uint32_t line_count;

    height_mm = app_abs_u32(max_y - min_y);
    line_count = ((height_mm + (uint32_t)lane_pitch_mm - 1U) / (uint32_t)lane_pitch_mm) + 1U;

    return line_count;
}

/* 计算第 index 条航线的 Y 坐标 */
static int32_t plan_calc_lane_y(int32_t min_y, int32_t max_y, uint16_t lane_pitch_mm, uint32_t index)
{
    int32_t line_y;

    /* 最后一条航线钳到边界，避免整除误差越界 */
    line_y = min_y + (int32_t)(index * lane_pitch_mm);
    if (line_y > max_y) {
        line_y = max_y;
    }

    return line_y;
}

/* 追加一个全局航点 */
static app_result_t plan_append_waypoint(plan_output_t *output,
                                         const app_point_t *point,
                                         plan_action_t action,
                                         uint16_t sub_batch_id)
{
    waypoint_t *waypoint;

    if ((output == 0) || (point == 0)) {
        return APP_ERR_PARAM;
    }

    if (output->waypoint_count >= APP_MAX_WAYPOINT_COUNT) {
        return APP_ERR_LIMIT;
    }

    waypoint = &output->waypoint[output->waypoint_count];
    waypoint->point = *point;
    waypoint->action = (uint8_t)action;
    waypoint->sub_batch_id = sub_batch_id;
    output->waypoint_count++;

    return APP_OK;
}

/* 追加航段终点，并把本段距离计入统计 */
static app_result_t plan_append_segment(plan_output_t *output,
                                        const app_point_t *point,
                                        plan_action_t action,
                                        uint16_t sub_batch_id,
                                        uint32_t *distance_mm)
{
    app_result_t result;

    if ((output == 0) || (point == 0) || (distance_mm == 0)) {
        return APP_ERR_PARAM;
    }

    /*
     * action 绑定在目标航点上，表示上一航点到当前航点这一段的动作
     */
    if (output->waypoint_count > 0U) {
        *distance_mm += plan_distance_mm(&output->waypoint[output->waypoint_count - 1U].point, point);
    }

    result = plan_append_waypoint(output, point, action, sub_batch_id);
    if (result != APP_OK) {
        return result;
    }

    return APP_OK;
}

/* 对同一主批次内的桶任务做空间排序 */
static void plan_sort_sub_batches(uint16_t *index_array,
                                  uint16_t count,
                                  const app_point_t *service_point,
                                  const mix_output_t *mix_output)
{
    uint16_t pos;

    /*
     * 每桶都会回到服务点，所以这里按服务点到桶中心的距离排序
     */
    if ((index_array == 0) || (service_point == 0) || (mix_output == 0) || (count <= 1U)) {
        return;
    }

    for (pos = 0U; pos < count; pos++) {
        uint16_t candidate;
        uint16_t best_pos;
        uint32_t best_distance;

        best_pos = pos;
        best_distance = 0xFFFFFFFFU;
        for (candidate = pos; candidate < count; candidate++) {
            const mix_sub_batch_t *sub_batch;
            app_point_t center;
            uint32_t distance;

            sub_batch = &mix_output->sub_batch[index_array[candidate]];
            center.x_mm = sub_batch->center_x_mm;
            center.y_mm = sub_batch->center_y_mm;
            distance = plan_distance_mm(service_point, &center);
            if ((distance < best_distance) ||
                ((distance == best_distance) && (index_array[candidate] < index_array[best_pos]))) {
                best_distance = distance;
                best_pos = candidate;
            }
        }

        if (best_pos != pos) {
            uint16_t temp;

            temp = index_array[pos];
            index_array[pos] = index_array[best_pos];
            index_array[best_pos] = temp;
        }
    }
}

/* 估算一个喷洒矩形会产生多少航点 */
static uint32_t plan_estimate_rect_waypoint_count(int32_t min_y, int32_t max_y, uint16_t lane_pitch_mm)
{
    uint32_t line_count;

    if (lane_pitch_mm == 0U) {
        return 0U;
    }

    line_count = plan_calc_s_lane_count(min_y, max_y, lane_pitch_mm);

    return (line_count * 2U);
}

/* 判断两个一维区间是否相接或重叠 */
static uint8_t plan_ranges_touch_or_overlap(int32_t a_min, int32_t a_max, int32_t b_min, int32_t b_max)
{
    /* 相接也可以合并 */
    return ((a_min <= b_max) && (b_min <= a_max)) ? 1U : 0U;
}

/* 把两个可合并喷洒块扩展成一个矩形范围 */
static void plan_merge_block_range(plan_spray_block_t *target, const plan_spray_block_t *source)
{
    if (source->min_x_mm < target->min_x_mm) {
        target->min_x_mm = source->min_x_mm;
    }
    if (source->max_x_mm > target->max_x_mm) {
        target->max_x_mm = source->max_x_mm;
    }
    if (source->min_y_mm < target->min_y_mm) {
        target->min_y_mm = source->min_y_mm;
    }
    if (source->max_y_mm > target->max_y_mm) {
        target->max_y_mm = source->max_y_mm;
    }
}

/* 尝试合并两个仍能保持矩形的喷洒块 */
static uint8_t plan_try_merge_blocks(plan_spray_block_t *target, const plan_spray_block_t *source)
{
    if ((target == 0) || (source == 0)) {
        return 0U;
    }

    /*
     * L 型或离散块不合并，防止把不该喷的区域包进去
     */
    if ((target->min_y_mm == source->min_y_mm) &&
        (target->max_y_mm == source->max_y_mm) &&
        plan_ranges_touch_or_overlap(target->min_x_mm, target->max_x_mm, source->min_x_mm, source->max_x_mm)) {
        plan_merge_block_range(target, source);
        return 1U;
    }

    if ((target->min_x_mm == source->min_x_mm) &&
        (target->max_x_mm == source->max_x_mm) &&
        plan_ranges_touch_or_overlap(target->min_y_mm, target->max_y_mm, source->min_y_mm, source->max_y_mm)) {
        plan_merge_block_range(target, source);
        return 1U;
    }

    return 0U;
}

/* 把 mix 的 chunk 整理成可喷洒矩形块 */
static app_result_t plan_build_spray_blocks(const mix_sub_batch_t *sub_batch,
                                            plan_spray_block_t *block,
                                            uint16_t *block_count)
{
    uint16_t i;
    uint8_t merged;

    if ((sub_batch == 0) || (block == 0) || (block_count == 0)) {
        return APP_ERR_PARAM;
    }

    /*
     * chunk 可能是完整地块，也可能是跨桶后的切片
     */
    *block_count = 0U;
    if (sub_batch->chunk_count == 0U) {
        block[0].min_x_mm = sub_batch->min_x_mm;
        block[0].max_x_mm = sub_batch->max_x_mm;
        block[0].min_y_mm = sub_batch->min_y_mm;
        block[0].max_y_mm = sub_batch->max_y_mm;
        *block_count = 1U;
        return APP_OK;
    }

    for (i = 0U; i < sub_batch->chunk_count; i++) {
        if (*block_count >= APP_MAX_MIX_CHUNK_PER_SUB_BATCH) {
            return APP_ERR_LIMIT;
        }
        block[*block_count].min_x_mm = sub_batch->chunk[i].min_x_mm;
        block[*block_count].max_x_mm = sub_batch->chunk[i].max_x_mm;
        block[*block_count].min_y_mm = sub_batch->chunk[i].min_y_mm;
        block[*block_count].max_y_mm = sub_batch->chunk[i].max_y_mm;
        (*block_count)++;
    }

    do {
        uint16_t left;
        uint16_t right;

        /* 多轮合并直到稳定，处理链式连续地块 */
        merged = 0U;
        for (left = 0U; (left < *block_count) && (merged == 0U); left++) {
            for (right = (uint16_t)(left + 1U); right < *block_count; right++) {
                if (plan_try_merge_blocks(&block[left], &block[right]) != 0U) {
                    uint16_t move_index;

                    /* 合并后右侧后面的块向前移动，填掉被合并的那个位置，保持 block_count 内都是有效块 */
                    for (move_index = right; (uint16_t)(move_index + 1U) < *block_count; move_index++) {
                        block[move_index] = block[move_index + 1U];
                    }
                    (*block_count)--;
                    merged = 1U;
                    break;
                }
            }
        }
    } while (merged != 0U);

    return APP_OK;
}

/* 获取喷洒块中心点 */
static app_point_t plan_block_center(const plan_spray_block_t *block)
{
    app_point_t center;

    center.x_mm = (block->min_x_mm + block->max_x_mm) / 2;
    center.y_mm = (block->min_y_mm + block->max_y_mm) / 2;

    return center;
}

/* 估算多个喷洒块的航点数量 */
static uint16_t plan_estimate_spray_blocks_waypoint_count(const plan_spray_block_t *block,uint16_t block_count,uint16_t lane_pitch_mm)
{
    uint16_t i;
    uint32_t count;

    count = 2U;
    for (i = 0U; i < block_count; i++) {
        count += plan_estimate_rect_waypoint_count(block[i].min_y_mm, block[i].max_y_mm, lane_pitch_mm);
    }
    if (count > 0xFFFFU) {
        return 0xFFFFU;
    }

    return (uint16_t)count;
}

/* 为一个矩形喷洒块生成 S 型喷洒航点 */
static app_result_t plan_append_spray_rect(plan_output_t *output,
                                           const plan_field_bounds_t *bounds,
                                           const app_point_t *reference_point,
                                           int32_t min_x_mm,
                                           int32_t max_x_mm,
                                           int32_t min_y_mm,
                                           int32_t max_y_mm,
                                           uint16_t lane_pitch_mm,
                                           uint16_t sub_batch_id,
                                           uint32_t *path_distance_mm,
                                           uint32_t *spray_distance_mm)
{
    int32_t start_side_x;
    int32_t other_side_x;
    int32_t spray_min_x;
    int32_t spray_max_x;
    int32_t top_y;
    int32_t bottom_y;
    uint32_t line_count;
    uint32_t i;
    app_result_t result;

    /*
     * 每条线先转场到起点，再用 SPRAY_ON 飞到终点
     */
    if ((output == 0) || (bounds == 0) || (reference_point == 0) ||
        (path_distance_mm == 0) || (spray_distance_mm == 0)) {
        return APP_ERR_PARAM;
    }

    /*把喷洒块边界钳制到农田边界内*/
    spray_min_x = app_clamp_i32(min_x_mm, bounds->min_x, bounds->max_x);
    spray_max_x = app_clamp_i32(max_x_mm, bounds->min_x, bounds->max_x);
    top_y = app_clamp_i32(min_y_mm, bounds->min_y, bounds->max_y);
    bottom_y = app_clamp_i32(max_y_mm, bounds->min_y, bounds->max_y);
    if ((spray_min_x >= spray_max_x) || (top_y > bottom_y)) {
        return APP_ERR_PARAM;
    }

    /* 从离上一航点更近的一侧进入喷洒块 */
    if (reference_point->x_mm <= ((spray_min_x + spray_max_x) / 2)) {
        start_side_x = spray_min_x;
        other_side_x = spray_max_x;
    } else {
        start_side_x = spray_max_x;
        other_side_x = spray_min_x;
    }

    /*循环生成每条 S 型航线*/
    line_count = plan_calc_s_lane_count(top_y, bottom_y, lane_pitch_mm);
    for (i = 0U; i < line_count; i++) {
        app_point_t spray_point;
        int32_t line_y;
        int32_t line_start_x;
        int32_t line_end_x;

        /*
         * 奇偶行交换起止 X 坐标，形成连续的 S 型覆盖
         */
        line_y = plan_calc_lane_y(top_y, bottom_y, lane_pitch_mm, i);
        if ((i & 0x01U) == 0U) {
            line_start_x = start_side_x;
            line_end_x = other_side_x;
        } else {
            line_start_x = other_side_x;
            line_end_x = start_side_x;
        }

        /* 追加 TRANSIT 航点 */
        spray_point.x_mm = line_start_x;
        spray_point.y_mm = line_y;
        result = plan_append_segment(output,&spray_point,PLAN_ACTION_TRANSIT,sub_batch_id,path_distance_mm);
        if (result != APP_OK) {
            return result;
        }


        /*追加 SPRAY_ON 航点*/
        spray_point.x_mm = line_end_x;
        result = plan_append_segment(output,&spray_point,PLAN_ACTION_SPRAY_ON,sub_batch_id,path_distance_mm);
        if (result != APP_OK) {
            return result;
        }

        *spray_distance_mm += app_abs_u32(line_end_x - line_start_x);
    }

    return APP_OK;
}

/* 为一个 mix 子批次生成闭环路径,是单桶路径生成 */
static app_result_t plan_build_sub_batch_path(plan_output_t *output,
                                              const plan_input_t *input,
                                              const plan_field_bounds_t *bounds,
                                              const mix_sub_batch_t *mix_sub_batch,
                                              sub_batch_t *plan_sub_batch,
                                              uint8_t need_refill,
                                              uint8_t need_change_liquid,
                                              uint8_t is_final_sub_batch,
                                              uint16_t lane_pitch_mm)
{
    app_point_t service_point;
    uint32_t i;
    uint32_t path_distance_mm;
    uint32_t spray_distance_mm;
    uint16_t waypoint_start;
    uint16_t estimated_count;
    app_result_t result;
    plan_spray_block_t block[APP_MAX_MIX_CHUNK_PER_SUB_BATCH];
    uint16_t block_count;

    /*每桶都从服务点出发，喷完后回服务点*/
    if ((output == 0) || (input == 0) || (bounds == 0) ||
        (mix_sub_batch == 0) || (plan_sub_batch == 0)) {
        return APP_ERR_PARAM;
    }

    result = plan_build_spray_blocks(mix_sub_batch, block, &block_count);
    if (result != APP_OK) {
        return result;
    }

    /*先估算航点数量，避免 output 里残留半条路径*/
    estimated_count = plan_estimate_spray_blocks_waypoint_count(block, block_count, lane_pitch_mm);
    if (estimated_count > APP_MAX_WAYPOINT_PER_SUB_BATCH) {
        return APP_ERR_LIMIT;
    }
    if (mix_sub_batch->liquid_ml_x10 > input->tank_capacity_ml_x10) {
        return APP_ERR_LIMIT;
    }

    /*首航点固定在服务点，动作区分起飞、续液和换液*/
    service_point = input->service_point;
    path_distance_mm = 0U;
    spray_distance_mm = 0U;
    waypoint_start = output->waypoint_count;
    plan_sub_batch->start_index = waypoint_start;

    if (need_refill != 0U) {
        result = plan_append_segment(output,&service_point,PLAN_ACTION_REFILL,plan_sub_batch->sub_batch_id,&path_distance_mm);
    } else if (need_change_liquid != 0U) {
        result = plan_append_segment(output,&service_point,PLAN_ACTION_CHANGE_LIQUID,plan_sub_batch->sub_batch_id,&path_distance_mm);
    } else {
        result = plan_append_segment(output,&service_point,PLAN_ACTION_TAKEOFF,plan_sub_batch->sub_batch_id,&path_distance_mm);
    }
    if (result != APP_OK) {
        return result;
    }

    /*桶内喷洒块排序准备*/
    {
        uint8_t block_used[APP_MAX_MIX_CHUNK_PER_SUB_BATCH];
        app_point_t reference_point;

        (void)memset(block_used, 0, sizeof(block_used));
        /*桶内喷洒块按离当前航点最近的顺序走*/
        reference_point = output->waypoint[output->waypoint_count - 1U].point;
        for (i = 0U; i < block_count; i++) {
            uint16_t block_index;
            uint16_t best_index;
            uint32_t best_distance;

            best_index = 0U;
            best_distance = 0xFFFFFFFFU;
            for (block_index = 0U; block_index < block_count; block_index++) {
                app_point_t center;
                uint32_t distance;

                if (block_used[block_index] != 0U) {
                    continue;
                }
                center = plan_block_center(&block[block_index]);
                distance = plan_distance_mm(&reference_point, &center);
                if (distance < best_distance) {
                    best_distance = distance;
                    best_index = block_index;
                }
            }
            block_used[best_index] = 1U;

            /*为选中喷洒块生成路径*/
            result = plan_append_spray_rect(output,
                                            bounds,
                                            &reference_point,
                                            block[best_index].min_x_mm,
                                            block[best_index].max_x_mm,
                                            block[best_index].min_y_mm,
                                            block[best_index].max_y_mm,
                                            lane_pitch_mm,
                                            plan_sub_batch->sub_batch_id,
                                            &path_distance_mm,
                                            &spray_distance_mm);
            if (result != APP_OK) {
                return result;
            }
            /* 下一块从当前结束点继续选最近块 */
            reference_point = output->waypoint[output->waypoint_count - 1U].point;
        }
    }

    /*追加返航航点,中间桶回服务点用 TRANSIT，只有最后一桶用 LAND*/
    result = plan_append_segment(output,
                                 &service_point,
                                 (is_final_sub_batch != 0U) ? PLAN_ACTION_LAND : PLAN_ACTION_TRANSIT,
                                 plan_sub_batch->sub_batch_id,
                                 &path_distance_mm);
    if (result != APP_OK) {
        return result;
    }

    /*写入本桶统计和全局统计,path_distance 统计全程，spray_distance 只统计 SPRAY_ON 段*/
    plan_sub_batch->waypoint_count = (uint16_t)(output->waypoint_count - waypoint_start);/*本桶航点数*/
    if (plan_sub_batch->waypoint_count > APP_MAX_WAYPOINT_PER_SUB_BATCH) {
        return APP_ERR_LIMIT;
    }
    plan_sub_batch->path_distance_mm = path_distance_mm;
    plan_sub_batch->spray_distance_mm = spray_distance_mm;
    plan_sub_batch->estimated_time_ms = (input->flight_speed_mmps > 0U) ?
                                        app_u32_from_u64(((uint64_t)path_distance_mm * 1000ULL) /
                                                          (uint64_t)input->flight_speed_mmps) : 0U;
    output->summary.total_distance_mm += path_distance_mm;
    output->summary.total_spray_distance_mm += spray_distance_mm;
    output->summary.total_estimated_time_ms += plan_sub_batch->estimated_time_ms;

    return APP_OK;
}

/* 当前无内部状态，保留模块初始化入口 */
void plan_module_init(void)
{
}

/* 把 mix 桶任务转换成全局航点数组和路径统计 */
app_result_t plan_module_run(const plan_input_t *input, plan_output_t *output)
{
    plan_field_bounds_t bounds;
    uint16_t lane_pitch_mm;
    uint16_t output_main_index;
    app_result_t result;

    if ((input == 0) || (output == 0) || (input->mix_output == 0)) {
        return APP_ERR_PARAM;
    }

    /* 每次规划都重建输出 */
    (void)memset(output, 0, sizeof(*output));
    output->summary.service_point = input->service_point;

    result = plan_get_field_bounds(&input->field, &bounds);
    if (result != APP_OK) {
        return result;
    }

    if ((input->spray_width_mm == 0U) || (input->flight_speed_mmps == 0U) || (input->tank_capacity_ml_x10 == 0U)) {
        return APP_ERR_PARAM;
    }

    /* 服务点离农田太近时不规划 */
    if (plan_distance_point_to_rect(&input->service_point, &bounds) < input->service_safe_distance_mm) {
        return APP_ERR_LIMIT;
    }


    /*计算航线行距和检查桶数量*/
    lane_pitch_mm = plan_calc_lane_pitch(input->spray_width_mm, input->spray_overlap_rate_x100);
    if (lane_pitch_mm == 0U) {
        return APP_ERR_PARAM;
    }

    if (input->mix_output->sub_batch_count > APP_MAX_SUB_BATCH_COUNT) {
        return APP_ERR_LIMIT;
    }

    /*遍历主批次*/
    output_main_index = 0U;
    for (output_main_index = 0U; output_main_index < input->mix_output->main_batch_count; output_main_index++) {
        const mix_main_batch_t *mix_main_batch;
        uint16_t sub_order[APP_MAX_MIX_SUB_BATCH_PER_MAIN];
        uint16_t local_sub_count;
        uint16_t local_index;

        /* 主批次不交叉执行，避免不同药液穿插 */
        mix_main_batch = &input->mix_output->main_batch[output_main_index];

        local_sub_count = mix_main_batch->sub_batch_count;
        if (local_sub_count > APP_MAX_MIX_SUB_BATCH_PER_MAIN) {
            return APP_ERR_LIMIT;
        }

        for (local_index = 0U; local_index < local_sub_count; local_index++) {
            sub_order[local_index] = (uint16_t)(mix_main_batch->sub_batch_start + local_index);
        }

        /*只在当前 main_batch 内排序，保持同一种药液连续执行*/
        plan_sort_sub_batches(sub_order, local_sub_count, &input->service_point, input->mix_output);

        for (local_index = 0U; local_index < local_sub_count; local_index++) {
            const mix_sub_batch_t *mix_sub_batch;
            sub_batch_t *plan_sub_batch;
            uint8_t need_refill;
            uint8_t need_change_liquid;
            uint8_t is_final_sub_batch;

            if (output->sub_batch_count >= APP_MAX_SUB_BATCH_COUNT) {
                return APP_ERR_LIMIT;
            }

            mix_sub_batch = &input->mix_output->sub_batch[sub_order[local_index]];
            
            /*补给动作按规划后的执行顺序判断*/
            need_refill = 0U;
            need_change_liquid = 0U;
            if (output->sub_batch_count > 0U) {
                const sub_batch_t *previous_plan_sub_batch;
                const mix_sub_batch_t *previous_mix_sub_batch;

                previous_plan_sub_batch = &output->sub_batch[output->sub_batch_count - 1U];
                previous_mix_sub_batch = &input->mix_output->sub_batch[previous_plan_sub_batch->mix_sub_batch_index];
                
                /*比较前后桶的真实喷洒内容和病害类型*/
                if ((previous_mix_sub_batch->spray_content != mix_sub_batch->spray_content) ||
                    (previous_mix_sub_batch->disease_type != mix_sub_batch->disease_type)) {
                    need_change_liquid = 1U;
                } else {
                    need_refill = 1U;
                }
            }

            /*判断当前桶是不是全局最后一桶*/
            is_final_sub_batch = (((uint16_t)(output_main_index + 1U) == input->mix_output->main_batch_count) &&
                                  ((uint16_t)(local_index + 1U) == local_sub_count)) ? 1U : 0U;

            /* 先准备 sub_batch 数据，路径生成成功后再提交计数 */
            plan_sub_batch = &output->sub_batch[output->sub_batch_count];
            plan_sub_batch->sub_batch_id = (uint16_t)(output->sub_batch_count + 1U);
            plan_sub_batch->mix_sub_batch_index = sub_order[local_index];

            /*调用单桶路径生成函数*/
            result = plan_build_sub_batch_path(output,
                                                input,
                                                &bounds,
                                                mix_sub_batch,
                                                plan_sub_batch,
                                                need_refill,
                                                need_change_liquid,
                                                is_final_sub_batch,
                                                lane_pitch_mm);
            if (result != APP_OK) {
                return result;
            }

            /* 整桶路径生成成功后再提交计数 */
            output->sub_batch_count++;
        }
    }

    return APP_OK;
}
