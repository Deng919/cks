#include <string.h>

#include "sim_module.h"
#include "app_math.h"
#include "data_table.h"

/*
 * 飞行仿真模块
 * 按规划航点推进位置；只有 SPRAY_ON 航段累计喷洒距离并扣减药液
 */

static sim_input_t g_sim_input;
static uint32_t g_sim_elapsed_ms;
static uint16_t g_sim_time_scale_x = APP_SIM_TIME_SCALE_X;

#define SIM_BATTERY_FULL_X100 10000U
#define SIM_BATTERY_LOW_X100 2500U
#define SIM_BATTERY_CHARGE_MS 5000U
#define SIM_REFILL_MS 3000U
#define SIM_RTH_SOURCE_LOW_LIQUID 1U
#define SIM_RTH_SOURCE_MANUAL 2U
#define SIM_RTH_SOURCE_LOW_BATTERY 3U

/* 仿真中断/补给状态下的起点记录，用以纠正无状态药量重新计算 */
static uint32_t g_sim_route_distance_at_refill_mm = 0U;
static uint16_t g_sim_last_sub_batch_index = 0xFFFFU;

static const uint16_t g_sim_time_scale_option[] = {
    5U,
    10U,
    20U,
    40U,
    80U,
    120U
};

static void sim_consume_battery_by_distance(sim_output_t *output, uint32_t distance_mm)
{
    uint32_t consumed_x100;

    if ((output == 0) || (distance_mm == 0U) || (g_sim_input.plan == 0) ||
        (g_sim_input.plan->summary.total_distance_mm == 0U)) {
        return;
    }

    consumed_x100 = (uint32_t)(((uint64_t)distance_mm * 900ULL) /
                               (uint64_t)g_sim_input.plan->summary.total_distance_mm);
    if (consumed_x100 == 0U) {
        consumed_x100 = 1U;
    }
    if (consumed_x100 >= output->battery_x100) {
        output->battery_x100 = 0U;
    } else {
        output->battery_x100 = (uint16_t)(output->battery_x100 - (uint16_t)consumed_x100);
    }
}

static void sim_consume_battery_by_tick(sim_output_t *output)
{
    uint16_t consumed_x100;

    if (output == 0) {
        return;
    }

    consumed_x100 = (uint16_t)((g_sim_time_scale_x / 4U) +
                               ((uint32_t)g_sim_time_scale_x * (uint32_t)g_sim_time_scale_x) / 240U);
    consumed_x100 = (uint16_t)(consumed_x100 / 5U);
    if (consumed_x100 == 0U) {
        consumed_x100 = 1U;
    }

    if (consumed_x100 >= output->battery_x100) {
        output->battery_x100 = 0U;
    } else {
        output->battery_x100 = (uint16_t)(output->battery_x100 - consumed_x100);
    }
}

/* 线性平移移动逻辑辅助函数 */
static void sim_move_towards(app_point_t *current, const app_point_t *target, uint32_t speed_per_tick_mm)
{
    int32_t dx = target->x_mm - current->x_mm;
    int32_t dy = target->y_mm - current->y_mm;
    uint32_t dist = app_point_distance_mm(current, target);
    if (dist <= speed_per_tick_mm || dist == 0) {
        *current = *target;
    } else {
        current->x_mm += (int32_t)(((int64_t)dx * speed_per_tick_mm) / dist);
        current->y_mm += (int32_t)(((int64_t)dy * speed_per_tick_mm) / dist);
    }
}

/* 计算相邻航点之间的航段距离 */
static uint32_t sim_segment_distance_mm(const waypoint_t *from, const waypoint_t *to)
{
    app_point_t from_point;
    app_point_t to_point;

    if ((from == 0) || (to == 0)) {
        return 0U;
    }

    from_point = from->point;
    to_point = to->point;
    return app_point_distance_mm(&from_point, &to_point);
}

/* 根据航段内已飞距离插值飞机位置 */
static app_point_t sim_interpolate_segment(const waypoint_t *from, const waypoint_t *to, uint32_t partial_mm)
{
    app_point_t point;
    uint32_t segment_mm;

    point = from->point;
    segment_mm = sim_segment_distance_mm(from, to);
    if (segment_mm == 0U) {
        return to->point;
    }
    if (partial_mm >= segment_mm) {
        return to->point;
    }

    /*
     * 用整数线性插值，避免引入浮点库
     */
    point.x_mm = from->point.x_mm +
                 (int32_t)(((int64_t)(to->point.x_mm - from->point.x_mm) * (int64_t)partial_mm) /
                           (int64_t)segment_mm);
    point.y_mm = from->point.y_mm +
                 (int32_t)(((int64_t)(to->point.y_mm - from->point.y_mm) * (int64_t)partial_mm) /
                           (int64_t)segment_mm);

    return point;
}

/* 通过当前航点反查所属桶任务 */
static const sub_batch_t *sim_find_current_sub_batch(uint16_t waypoint_index, uint16_t *sub_batch_index)
{
    uint16_t i;

    if ((g_sim_input.plan == 0) || (g_sim_input.plan->sub_batch_count == 0U)) {
        return 0;
    }

    for (i = 0U; i < g_sim_input.plan->sub_batch_count; i++) {
        const sub_batch_t *sub_batch;
        uint16_t end_index;

        /*判断航点是否属于当前桶任务*/
        sub_batch = &g_sim_input.plan->sub_batch[i];
        end_index = (uint16_t)(sub_batch->start_index + sub_batch->waypoint_count);
        if ((waypoint_index >= sub_batch->start_index) && (waypoint_index < end_index)) {
            if (sub_batch_index != 0) {
                *sub_batch_index = i;
            }
            return sub_batch;
        }
    }

    return 0;
}

/* 计算当前桶在全局路径中的起点里程,把全局已飞距离转换成桶内已飞距离 */
static uint32_t sim_calc_sub_batch_route_start_mm(uint16_t sub_batch_index)
{
    uint16_t i;
    uint32_t route_start_mm;

    if (g_sim_input.plan == 0) {
        return 0U;
    }

    route_start_mm = 0U;
    for (i = 0U; (i < sub_batch_index) && (i < g_sim_input.plan->sub_batch_count); i++) {
        route_start_mm += g_sim_input.plan->sub_batch[i].path_distance_mm;
    }

    return route_start_mm;
}

/* 统计当前桶内已经喷洒的距离 */
static uint32_t sim_calc_sub_batch_spray_done_mm(const sub_batch_t *sub_batch,uint32_t sub_route_done_mm)
{
    uint16_t i;
    uint16_t end_index;
    uint32_t travelled_mm;
    uint32_t spray_done_mm;

    if ((sub_batch == 0) || (g_sim_input.plan == 0) || (sub_batch->waypoint_count < 2U)) {
        return 0U;
    }

    /*只有 SPRAY_ON 段消耗药液，转场和补给动作不算喷洒*/
    travelled_mm = 0U;
    spray_done_mm = 0U;
    end_index = (uint16_t)(sub_batch->start_index + sub_batch->waypoint_count);
    for (i = (uint16_t)(sub_batch->start_index + 1U); i < end_index; i++) {
        uint32_t segment_mm;
        uint32_t next_travelled_mm;

        segment_mm = sim_segment_distance_mm(&g_sim_input.plan->waypoint[i - 1U],&g_sim_input.plan->waypoint[i]);
        next_travelled_mm = travelled_mm + segment_mm;

        /*当前航段已经完整走完*/
        if (sub_route_done_mm >= next_travelled_mm) {
            if (g_sim_input.plan->waypoint[i].action == PLAN_ACTION_SPRAY_ON) {
                spray_done_mm += segment_mm;
            }
            /*更新已扫描桶内距离*/
            travelled_mm = next_travelled_mm;
            continue;
        }

        /*当前航段只走了一部分*/
        if ((sub_route_done_mm > travelled_mm) &&
            (g_sim_input.plan->waypoint[i].action == PLAN_ACTION_SPRAY_ON)) {
            spray_done_mm += sub_route_done_mm - travelled_mm;
        }
        break;
    }

    if (spray_done_mm > sub_batch->spray_distance_mm) {
        spray_done_mm = sub_batch->spray_distance_mm;
    }

    return spray_done_mm;
}

/* 根据当前桶喷洒进度估算机载剩余液量 (考虑补给点起飞的累计清空) */
static uint32_t sim_calc_current_carried_liquid_ml_x10(uint16_t waypoint_index, uint32_t route_distance_done_mm, uint32_t route_distance_at_refill_mm)
{
    const sub_batch_t *plan_sub_batch;
    const mix_sub_batch_t *mix_sub_batch;
    uint16_t sub_batch_index;
    uint32_t route_start_mm;
    uint32_t sub_route_done_mm;
    uint32_t sub_spray_done_mm;
    uint32_t sub_route_at_refill_mm;
    uint32_t sub_spray_at_refill_mm;
    uint64_t consumed;

    plan_sub_batch = sim_find_current_sub_batch(waypoint_index, &sub_batch_index);
    if ((plan_sub_batch == 0) || (g_sim_input.mix_output == 0) ||
        (plan_sub_batch->mix_sub_batch_index >= g_sim_input.mix_output->sub_batch_count)) {
        return 0U;
    }

    mix_sub_batch = &g_sim_input.mix_output->sub_batch[plan_sub_batch->mix_sub_batch_index];
    if ((mix_sub_batch->liquid_ml_x10 == 0U) || (plan_sub_batch->spray_distance_mm == 0U)) {
        return mix_sub_batch->liquid_ml_x10;
    }

    route_start_mm = sim_calc_sub_batch_route_start_mm(sub_batch_index);
    if (route_distance_done_mm <= route_start_mm) {
        return mix_sub_batch->liquid_ml_x10;
    }

    sub_route_done_mm = route_distance_done_mm - route_start_mm;
    if (sub_route_done_mm > plan_sub_batch->path_distance_mm) {
        sub_route_done_mm = plan_sub_batch->path_distance_mm;
    }
    sub_spray_done_mm = sim_calc_sub_batch_spray_done_mm(plan_sub_batch, sub_route_done_mm);

    /* 计算上次补给时的已喷洒量 */
    sub_spray_at_refill_mm = 0U;
    if (route_distance_at_refill_mm > route_start_mm) {
        sub_route_at_refill_mm = route_distance_at_refill_mm - route_start_mm;
        if (sub_route_at_refill_mm > plan_sub_batch->path_distance_mm) {
            sub_route_at_refill_mm = plan_sub_batch->path_distance_mm;
        }
        sub_spray_at_refill_mm = sim_calc_sub_batch_spray_done_mm(plan_sub_batch, sub_route_at_refill_mm);
    }

    /* 仅计算自上次补给以来的消耗量 */
    if (sub_spray_done_mm <= sub_spray_at_refill_mm) {
        return mix_sub_batch->liquid_ml_x10;
    }

    consumed = (uint64_t)mix_sub_batch->liquid_ml_x10 * (uint64_t)(sub_spray_done_mm - sub_spray_at_refill_mm);
    consumed /= (uint64_t)plan_sub_batch->spray_distance_mm;
    if (consumed >= mix_sub_batch->liquid_ml_x10) {
        return 0U;
    }

    return mix_sub_batch->liquid_ml_x10 - (uint32_t)consumed;
}

static uint8_t sim_should_auto_return_for_liquid(const sim_output_t *output)
{
    const sub_batch_t *plan_sub_batch;
    const mix_sub_batch_t *mix_sub_batch;
    uint16_t sub_batch_index;
    uint16_t mix_index;
    uint32_t route_start_mm;
    uint32_t sub_route_done_mm;
    uint32_t sub_spray_done_mm;
    uint32_t remaining_spray_mm;
    uint32_t required_liquid_ml_x10;
    uint64_t required_liquid;

    if ((output == 0) || (g_sim_input.plan == 0) || (g_sim_input.mix_output == 0)) {
        return 0U;
    }
    if (output->current_waypoint_index >= g_sim_input.plan->waypoint_count) {
        return 0U;
    }
    if (g_sim_input.plan->waypoint[output->current_waypoint_index].action != PLAN_ACTION_SPRAY_ON) {
        return 0U;
    }

    plan_sub_batch = sim_find_current_sub_batch(output->current_waypoint_index, &sub_batch_index);
    if (plan_sub_batch == 0) {
        return 0U;
    }
    mix_index = plan_sub_batch->mix_sub_batch_index;
    if (mix_index >= g_sim_input.mix_output->sub_batch_count) {
        return 0U;
    }

    mix_sub_batch = &g_sim_input.mix_output->sub_batch[mix_index];
    if ((mix_sub_batch->liquid_ml_x10 == 0U) || (plan_sub_batch->spray_distance_mm == 0U)) {
        return 0U;
    }

    route_start_mm = sim_calc_sub_batch_route_start_mm(sub_batch_index);
    if (output->route_distance_done_mm <= route_start_mm) {
        return 0U;
    }

    sub_route_done_mm = output->route_distance_done_mm - route_start_mm;
    if (sub_route_done_mm > plan_sub_batch->path_distance_mm) {
        sub_route_done_mm = plan_sub_batch->path_distance_mm;
    }
    sub_spray_done_mm = sim_calc_sub_batch_spray_done_mm(plan_sub_batch, sub_route_done_mm);
    if (sub_spray_done_mm >= plan_sub_batch->spray_distance_mm) {
        return 0U;
    }

    remaining_spray_mm = plan_sub_batch->spray_distance_mm - sub_spray_done_mm;
    required_liquid = (uint64_t)mix_sub_batch->liquid_ml_x10 * (uint64_t)remaining_spray_mm;
    required_liquid += (uint64_t)plan_sub_batch->spray_distance_mm - 1ULL;
    required_liquid /= (uint64_t)plan_sub_batch->spray_distance_mm;
    required_liquid_ml_x10 = app_u32_from_u64(required_liquid);

    return (output->remain_liquid_ml_x10 < required_liquid_ml_x10) ? 1U : 0U;
}

static uint8_t sim_should_auto_return_for_battery(const sim_output_t *output)
{
    if (output == 0) {
        return 0U;
    }

    return (output->battery_x100 <= SIM_BATTERY_LOW_X100) ? 1U : 0U;
}

/* 把仿真时间映射为全局已飞距离 */
static uint32_t sim_calc_route_distance_done_mm(uint32_t elapsed_ms)
{
    uint64_t distance;

    if ((g_sim_input.plan == 0) || (g_sim_input.plan->summary.total_distance_mm == 0U)) {
        return 0U;
    }

    if (g_sim_input.plan->summary.total_estimated_time_ms == 0U) {
        return g_sim_input.plan->summary.total_distance_mm;
    }

    /*仿真时间超过总预计时间*/
    if (elapsed_ms >= g_sim_input.plan->summary.total_estimated_time_ms) {
        return g_sim_input.plan->summary.total_distance_mm;
    }

    /* 先按总时长映射到距离，再由距离定位航段 */
    distance = (uint64_t)g_sim_input.plan->summary.total_distance_mm * (uint64_t)elapsed_ms;
    distance /= (uint64_t)g_sim_input.plan->summary.total_estimated_time_ms;
    if (distance > g_sim_input.plan->summary.total_distance_mm) {
        return g_sim_input.plan->summary.total_distance_mm;
    }

    return (uint32_t)distance;
}

/* 根据已飞距离刷新位置、航点和喷洒里程 */
static void sim_update_output_by_distance(sim_output_t *output, uint32_t route_distance_done_mm)
{
    uint16_t i;
    uint16_t current_index;
    uint32_t travelled_mm;
    uint32_t spray_done_mm;

    if ((output == 0) || (g_sim_input.plan == 0) || (g_sim_input.plan->waypoint_count == 0U)) {
        return;
    }

    /*到达航点时取航点坐标，位于航段中间时做插值*/
    travelled_mm = 0U;
    spray_done_mm = 0U;
    current_index = 0U;
    output->current_position = g_sim_input.plan->waypoint[0].point;


    /*遍历全局航段*/
    for (i = 1U; i < g_sim_input.plan->waypoint_count; i++) {
        uint32_t segment_mm;
        uint32_t next_travelled_mm;

        segment_mm = sim_segment_distance_mm(&g_sim_input.plan->waypoint[i - 1U],
                                             &g_sim_input.plan->waypoint[i]);
        next_travelled_mm = travelled_mm + segment_mm;
        /*当前航段已完整完成*/
        if (route_distance_done_mm >= next_travelled_mm) {
            /*action 绑在目标航点上，SPRAY_ON 表示这一整段都在喷洒*/
            if (g_sim_input.plan->waypoint[i].action == PLAN_ACTION_SPRAY_ON) {
                spray_done_mm += segment_mm;
            }
            travelled_mm = next_travelled_mm;
            current_index = i;
            output->current_position = g_sim_input.plan->waypoint[i].point;
            continue;
        }

        /*当前距离落在航段中间*/
        if (route_distance_done_mm > travelled_mm) {
            uint32_t partial_mm;

            /*航段中间只累计 partial_mm，并用插值更新位置*/
            partial_mm = route_distance_done_mm - travelled_mm;
            if (g_sim_input.plan->waypoint[i].action == PLAN_ACTION_SPRAY_ON) {
                spray_done_mm += partial_mm;
            }
            current_index = i;
            output->current_position = sim_interpolate_segment(&g_sim_input.plan->waypoint[i - 1U],
                                                               &g_sim_input.plan->waypoint[i],
                                                               partial_mm);
        }
        break;
    }

    /*路线完成处理*/
    if (route_distance_done_mm >= g_sim_input.plan->summary.total_distance_mm) {
        /* 到达终点时锁定最后航点，避免插值误差残留 */
        current_index = (uint16_t)(g_sim_input.plan->waypoint_count - 1U);
        spray_done_mm = g_sim_input.plan->summary.total_spray_distance_mm;
        output->current_position = g_sim_input.plan->waypoint[current_index].point;
    }

    if (spray_done_mm > g_sim_input.plan->summary.total_spray_distance_mm) {
        spray_done_mm = g_sim_input.plan->summary.total_spray_distance_mm;
    }

    /* 集中输出，UI 只读取这一份实时状态 */
    output->current_waypoint_index = current_index;
    output->elapsed_time_ms = g_sim_elapsed_ms;
    output->route_distance_done_mm = route_distance_done_mm;
    output->spray_distance_done_mm = spray_done_mm;
}

/* 初始化仿真输入引用和计时 */
void sim_module_init(void)
{
    (void)memset(&g_sim_input, 0, sizeof(g_sim_input));
    g_sim_elapsed_ms = 0U;
    g_sim_time_scale_x = APP_SIM_TIME_SCALE_X;
    g_sim_route_distance_at_refill_mm = 0U;
    g_sim_last_sub_batch_index = 0xFFFFU;
}

/* 绑定 plan/mix 输出并初始化实时仿真状态 */
app_result_t sim_module_start(const sim_input_t *input, sim_output_t *output)
{
    /* start 不重新规划路径，只保存输入引用 */
    if ((input == 0) || (output == 0) || (input->plan == 0) || (input->mix_output == 0)) {
        return APP_ERR_PARAM;
    }
    if ((input->plan->waypoint_count == 0U) ||
        (input->plan->summary.total_distance_mm == 0U) ||
        (input->plan->summary.total_spray_distance_mm == 0U)) {
        return APP_ERR_STATE;
    }

    if (output->running != 0U) {
        return APP_OK;
    }

    /* 后续 tick 直接读取这些引用，不复制大数组 */
    g_sim_input = *input;

    if (output->state == SIM_STATE_PAUSED) {
        output->running = 1U;
        if ((output->pause_resume_state >= (uint8_t)SIM_STATE_SPRAYING) &&
            (output->pause_resume_state <= (uint8_t)SIM_STATE_RETURNING_TO_BREAKPOINT)) {
            output->state = (sim_state_t)output->pause_resume_state;
        } else {
            output->state = SIM_STATE_SPRAYING;
        }
        return APP_OK;
    }

    g_sim_elapsed_ms = 0U;
    (void)memset(output, 0, sizeof(*output));
    output->running = 1U;
    output->state = SIM_STATE_SPRAYING; /* 初始状态为作业中 */
    output->pause_resume_state = (uint8_t)SIM_STATE_SPRAYING;
    output->battery_x100 = SIM_BATTERY_FULL_X100;
    output->rth_trigger_source = 0U;
    g_sim_route_distance_at_refill_mm = 0U;
    g_sim_last_sub_batch_index = 0xFFFFU;
    if (input->plan->waypoint_count > 0U) {
        /*把初始位置设置为第一个航点*/
        output->current_position = input->plan->waypoint[0].point;
    }
    output->remain_liquid_ml_x10 = sim_calc_current_carried_liquid_ml_x10(0U, 0U, 0U);

    return APP_OK;
}

/* 按 10ms 间隔推进仿真输出 */
app_result_t sim_module_tick_10ms(sim_output_t *output)
{
    if (output == 0) {
        return APP_ERR_PARAM;
    }

    /* 非运行状态直接返回，主循环可以一直调用 */
    if (output->running == 0U) {
        return APP_OK;
    }

    if ((g_sim_input.plan == 0) || (g_sim_input.plan->waypoint_count == 0U) ||
        (g_sim_input.plan->summary.total_distance_mm == 0U) ||
        (g_sim_input.plan->summary.total_spray_distance_mm == 0U)) {
        output->running = 0U;
        output->alarm[0].code = APP_ALARM_INVALID_DATA;
        output->alarm_count = 1U;
        return APP_ERR_STATE;
    }

    uint16_t current_sub_batch_idx = 0xFFFFU;
    (void)sim_find_current_sub_batch(output->current_waypoint_index, &current_sub_batch_idx);
    if (current_sub_batch_idx != g_sim_last_sub_batch_index) {
        g_sim_last_sub_batch_index = current_sub_batch_idx;
        g_sim_route_distance_at_refill_mm = 0U;
    }

    uint32_t speed_per_tick_mm = (g_data_default_drone.flight_speed_mmps * APP_TICK_PERIOD_MS * g_sim_time_scale_x) / 1000U;

    switch (output->state) {
        case SIM_STATE_SPRAYING: {
            uint32_t route_before_mm;

            route_before_mm = output->route_distance_done_mm;
            if (g_sim_elapsed_ms <= (0xFFFFFFFFUL - (APP_TICK_PERIOD_MS * g_sim_time_scale_x))) {
                g_sim_elapsed_ms += (APP_TICK_PERIOD_MS * g_sim_time_scale_x);
            } else {
                g_sim_elapsed_ms = 0xFFFFFFFFUL;
            }

            sim_update_output_by_distance(output, sim_calc_route_distance_done_mm(g_sim_elapsed_ms));
            sim_consume_battery_by_tick(output);
            if (output->route_distance_done_mm > route_before_mm) {
                sim_consume_battery_by_distance(output, output->route_distance_done_mm - route_before_mm);
            }

            output->progress_x100 = (uint16_t)(((uint64_t)output->route_distance_done_mm * 10000ULL) /
                                               (uint64_t)g_sim_input.plan->summary.total_distance_mm);
            if (output->progress_x100 > 10000U) {
                output->progress_x100 = 10000U;
            }
            if (g_sim_input.plan->summary.total_spray_distance_mm > 0U) {
                output->cover_rate_x100 = (uint16_t)(((uint64_t)output->spray_distance_done_mm * 10000ULL) /
                                                     (uint64_t)g_sim_input.plan->summary.total_spray_distance_mm);
            } else {
                output->cover_rate_x100 = 0U;
            }
            if (output->cover_rate_x100 > 10000U) {
                output->cover_rate_x100 = 10000U;
            }

            output->remain_liquid_ml_x10 = sim_calc_current_carried_liquid_ml_x10(output->current_waypoint_index, output->route_distance_done_mm, g_sim_route_distance_at_refill_mm);

            if (sim_should_auto_return_for_liquid(output) != 0U) {
                output->state = SIM_STATE_RETURNING_TO_HOME;
                output->rth_trigger_source = SIM_RTH_SOURCE_LOW_LIQUID;
                output->breakpoint_position = output->current_position;
            } else if (sim_should_auto_return_for_battery(output) != 0U) {
                output->state = SIM_STATE_RETURNING_TO_HOME;
                output->rth_trigger_source = SIM_RTH_SOURCE_LOW_BATTERY;
                output->breakpoint_position = output->current_position;
            }

            if (output->route_distance_done_mm >= g_sim_input.plan->summary.total_distance_mm) {
                output->running = 0U;
                output->alarm[0].code = APP_ALARM_ROUTE_FINISHED;
                output->alarm_count = 1U;
            }
            break;
        }

        case SIM_STATE_RETURNING_TO_HOME: {
            app_point_t old_position;

            old_position = output->current_position;
            sim_move_towards(&output->current_position, &g_sim_input.plan->summary.service_point, speed_per_tick_mm);
            sim_consume_battery_by_tick(output);
            sim_consume_battery_by_distance(output, app_point_distance_mm(&old_position, &output->current_position));
            if ((output->current_position.x_mm == g_sim_input.plan->summary.service_point.x_mm) &&
                (output->current_position.y_mm == g_sim_input.plan->summary.service_point.y_mm)) {
                output->state = SIM_STATE_REFILLING;
                output->refill_timer_ms = (output->rth_trigger_source == SIM_RTH_SOURCE_LOW_BATTERY) ?
                                           SIM_BATTERY_CHARGE_MS : SIM_REFILL_MS;
                if (output->rth_trigger_source == SIM_RTH_SOURCE_LOW_BATTERY) {
                    if (output->charge_count < 0xFFFFU) {
                        output->charge_count++;
                    }
                } else if (output->refill_count < 0xFFFFU) {
                    output->refill_count++;
                }
            }
            break;
        }

        case SIM_STATE_REFILLING: {
            uint32_t elapsed = APP_TICK_PERIOD_MS * g_sim_time_scale_x;
            if (output->refill_timer_ms > elapsed) {
                output->refill_timer_ms -= elapsed;
            } else {
                output->refill_timer_ms = 0U;
            }

            if (output->refill_timer_ms == 0U) {
                uint16_t sub_idx = 0U;
                const sub_batch_t *sub_batch = sim_find_current_sub_batch(output->current_waypoint_index, &sub_idx);
                if (sub_batch != 0 && g_sim_input.mix_output != 0) {
                    uint16_t mix_index = sub_batch->mix_sub_batch_index;
                    if (mix_index < g_sim_input.mix_output->sub_batch_count) {
                        output->remain_liquid_ml_x10 = g_sim_input.mix_output->sub_batch[mix_index].liquid_ml_x10;
                    }
                }
                output->battery_x100 = SIM_BATTERY_FULL_X100;
                g_sim_route_distance_at_refill_mm = output->route_distance_done_mm;
                output->state = SIM_STATE_RETURNING_TO_BREAKPOINT;
            }
            break;
        }

        case SIM_STATE_RETURNING_TO_BREAKPOINT: {
            app_point_t old_position;

            old_position = output->current_position;
            sim_move_towards(&output->current_position, &output->breakpoint_position, speed_per_tick_mm);
            sim_consume_battery_by_tick(output);
            sim_consume_battery_by_distance(output, app_point_distance_mm(&old_position, &output->current_position));
            if ((output->current_position.x_mm == output->breakpoint_position.x_mm) &&
                (output->current_position.y_mm == output->breakpoint_position.y_mm)) {
                output->state = SIM_STATE_SPRAYING;
                output->rth_trigger_source = 0U;
            }
            break;
        }

        case SIM_STATE_PAUSED:
        case SIM_STATE_IDLE:
        default: {
            break;
        }
    }

    return APP_OK;
}

/* 停止运行标志，保留当前位置和进度 */
app_result_t sim_module_stop(sim_output_t *output)
{
    if (output == 0) {
        return APP_ERR_PARAM;
    }

    if (output->state == SIM_STATE_IDLE) {
        return APP_ERR_STATE;
    }

    if (output->state != SIM_STATE_PAUSED) {
        output->pause_resume_state = (uint8_t)output->state;
    }
    output->state = SIM_STATE_PAUSED;
    output->running = 0U;
    return APP_OK;
}

/* 清空旧仿真输出，等待新规划启动 */
void sim_module_reset(sim_output_t *output)
{
    if (output != 0) {
        (void)memset(output, 0, sizeof(*output));
    }
    g_sim_elapsed_ms = 0U;
    g_sim_route_distance_at_refill_mm = 0U;
    g_sim_last_sub_batch_index = 0xFFFFU;
}

/* 当前演示倍速 */
uint16_t sim_module_get_time_scale(void)
{
    return g_sim_time_scale_x;
}

/* 切换到下一个演示倍速 */
uint16_t sim_module_cycle_time_scale(void)
{
    uint16_t i;
    uint16_t option_count;

    option_count = (uint16_t)(sizeof(g_sim_time_scale_option) / sizeof(g_sim_time_scale_option[0]));
    /*倍速只改变后续 tick 的时间增量*/
    for (i = 0U; i < option_count; i++) {
        if (g_sim_time_scale_option[i] == g_sim_time_scale_x) {
            g_sim_time_scale_x = g_sim_time_scale_option[(uint16_t)((i + 1U) % option_count)];
            return g_sim_time_scale_x;
        }
    }

    g_sim_time_scale_x = APP_SIM_TIME_SCALE_X;
    return g_sim_time_scale_x;
}

app_result_t sim_module_trigger_recall(sim_output_t *output)
{
    if (output == 0) {
        return APP_ERR_PARAM;
    }
    if ((output->state == SIM_STATE_PAUSED) && (output->pause_resume_state == (uint8_t)SIM_STATE_SPRAYING)) {
        output->running = 1U;
    }
    if ((output->state == SIM_STATE_SPRAYING) || (output->state == SIM_STATE_PAUSED)) {
        output->running = 1U;
        output->state = SIM_STATE_RETURNING_TO_HOME;
        output->rth_trigger_source = SIM_RTH_SOURCE_MANUAL;
        output->breakpoint_position = output->current_position;
        return APP_OK;
    }
    if (output->running == 0U) {
        return APP_ERR_PARAM;
    }
    if (output->state == SIM_STATE_RETURNING_TO_HOME) {
        output->rth_trigger_source = SIM_RTH_SOURCE_MANUAL;
        return APP_OK;
    }
    return APP_ERR_STATE;
}

app_result_t sim_module_resume_from_refill(sim_output_t *output)
{
    if (output == 0 || output->running == 0U) {
        return APP_ERR_PARAM;
    }
    if (output->state == SIM_STATE_RETURNING_TO_BREAKPOINT) {
        return APP_OK;
    }
    if (output->state == SIM_STATE_REFILLING) {
        output->refill_timer_ms = 0U;
        /* 补满药量 */
        uint16_t sub_idx = 0U;
        const sub_batch_t *sub_batch = sim_find_current_sub_batch(output->current_waypoint_index, &sub_idx);
        if (sub_batch != 0 && g_sim_input.mix_output != 0) {
            uint16_t mix_index = sub_batch->mix_sub_batch_index;
            if (mix_index < g_sim_input.mix_output->sub_batch_count) {
                output->remain_liquid_ml_x10 = g_sim_input.mix_output->sub_batch[mix_index].liquid_ml_x10;
            }
        }
        output->battery_x100 = SIM_BATTERY_FULL_X100;
        g_sim_route_distance_at_refill_mm = output->route_distance_done_mm;
        output->state = SIM_STATE_RETURNING_TO_BREAKPOINT;
        return APP_OK;
    }
    return APP_ERR_STATE;
}
