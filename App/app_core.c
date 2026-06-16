/* 主流程调度和运行态保存 */
#include <string.h>

#include "app_core.h"
#include "data_table.h"
#include "mix_module.h"
#include "monitor_module.h"
#include "plan_module.h"
#include "sim_module.h"

/*
 * UI 只和 app_core 交互，算法模块之间不互相调用
 * 上游结果变化时，这里负责清掉下游缓存
 */
static app_runtime_t g_app_runtime;

/*
 * 监测结果是最上游数据，地块状态一变，配药、路径和仿真都不能沿用
 */
static void app_core_clear_downstream_from_monitor(void)
{
    (void)memset(&g_app_runtime.mix_input, 0, sizeof(g_app_runtime.mix_input));
    (void)memset(&g_app_runtime.mix_output, 0, sizeof(g_app_runtime.mix_output));
    (void)memset(&g_app_runtime.plan_output, 0, sizeof(g_app_runtime.plan_output));
    sim_module_reset(&g_app_runtime.sim_output);
}

/*
 * 配药变化不会影响监测结果，但会让后面的路径和仿真失效
 */
static void app_core_clear_downstream_from_mix(void)
{
    (void)memset(&g_app_runtime.plan_output, 0, sizeof(g_app_runtime.plan_output));
    sim_module_reset(&g_app_runtime.sim_output);
}

/* 启动时清空运行态，并让各模块做自己的初始化 */
void app_core_init(void)
{
    (void)memset(&g_app_runtime, 0, sizeof(g_app_runtime));
    monitor_module_init();
    mix_module_init();
    plan_module_init();
    sim_module_init();
    g_app_runtime.state = APP_STATE_IDLE;
}

/* 10ms 业务节拍，目前只推进运行中的仿真 */
void app_core_tick_10ms(void)
{
    if (g_app_runtime.state == APP_STATE_SIM_RUNNING) {
        app_result_t result;

        result = sim_module_tick_10ms(&g_app_runtime.sim_output);
        /* tick 出错也交给 UI 按“已结束”状态处理 */
        if ((result != APP_OK) ||
            ((g_app_runtime.sim_output.running == 0U) &&
             (g_app_runtime.sim_output.state != SIM_STATE_PAUSED))) {
            g_app_runtime.state = APP_STATE_SIM_DONE;
        }
    }
}

/* 完整检测会重建 monitor_output，成功后下游结果作废 */
app_result_t app_core_run_monitor(const monitor_input_t *input)
{
    app_result_t result;

    if (input == 0) {
        return APP_ERR_PARAM;
    }

    g_app_runtime.monitor_input = *input;
    result = monitor_module_run(&g_app_runtime.monitor_input, &g_app_runtime.monitor_output);
    if (result != APP_OK) {
        return result;
    }

    app_core_clear_downstream_from_monitor();
    g_app_runtime.state = APP_STATE_MONITOR_DONE;
    return APP_OK;
}

/* 手动改已有地块；如果还没有检测结果，就先按输入生成一次 */
app_result_t app_core_apply_manual_monitor(const monitor_input_t *input)
{
    app_result_t result;

    if (input == 0) {
        return APP_ERR_PARAM;
    }

    if (g_app_runtime.monitor_output.plot_count == 0U) {
        return app_core_run_monitor(input);
    }

    g_app_runtime.monitor_input = *input;
    result = monitor_module_apply_manual(&g_app_runtime.monitor_input, &g_app_runtime.monitor_output);
    if (result != APP_OK) {
        return result;
    }

    app_core_clear_downstream_from_monitor();
    g_app_runtime.state = APP_STATE_MONITOR_DONE;
    return APP_OK;
}

/* 根据当前检测结果执行配药拆桶 */
app_result_t app_core_run_mix_with_input(const mix_input_t *input)
{
    app_result_t result;

    /* 配药只依赖检测结果和药箱容量 */
    if ((input == 0) || (input->tank_capacity_ml_x10 == 0U)) {
        return APP_ERR_PARAM;
    }

    /* 允许在配药、规划、仿真结束后重新按同一监测结果配药 */
    if ((g_app_runtime.state != APP_STATE_MONITOR_DONE) &&
        (g_app_runtime.state != APP_STATE_MIX_DONE) &&
        (g_app_runtime.state != APP_STATE_PLAN_DONE) &&
        (g_app_runtime.state != APP_STATE_SIM_DONE)) {
        return APP_ERR_STATE;
    }

    if (g_app_runtime.monitor_output.plot_count == 0U) {
        return APP_ERR_STATE;
    }

    g_app_runtime.mix_input = *input;
    result = mix_module_run(&g_app_runtime.monitor_output, input, &g_app_runtime.mix_output);
    if (result != APP_OK) {
        return result;
    }

    app_core_clear_downstream_from_mix();

    g_app_runtime.state = APP_STATE_MIX_DONE;
    return APP_OK;
}

/* 组装规划输入，把拆桶结果转换成航点路径 */
app_result_t app_core_run_plan(void)
{
    plan_input_t plan_input;
    app_result_t result;

    /*
     * plan_module 不直接读全局默认值，所有输入在这里摆齐
     * 这样规划失败时更容易看清是哪一类参数不对
     */
    if ((g_app_runtime.state != APP_STATE_MIX_DONE) &&
        (g_app_runtime.state != APP_STATE_PLAN_DONE) &&
        (g_app_runtime.state != APP_STATE_SIM_DONE)) {
        return APP_ERR_STATE;
    }

    if ((g_app_runtime.mix_output.main_batch_count == 0U) || (g_app_runtime.mix_output.sub_batch_count == 0U)) {
        return APP_ERR_STATE;
    }

    (void)memset(&plan_input, 0, sizeof(plan_input));
    /* 规划输入由运行态结果和数据表默认参数拼成 */
    plan_input.mix_output = &g_app_runtime.mix_output;
    plan_input.field = g_app_runtime.monitor_output.field;
    plan_input.service_point = g_data_default_service_point;
    plan_input.service_safe_distance_mm = g_data_default_plan.service_safe_distance_mm;
    plan_input.spray_width_mm = g_data_default_drone.spray_width_mm;
    plan_input.spray_overlap_rate_x100 = g_data_default_plan.spray_overlap_rate_x100;
    plan_input.flight_speed_mmps = g_data_default_drone.flight_speed_mmps;
    plan_input.tank_capacity_ml_x10 = g_app_runtime.mix_input.tank_capacity_ml_x10;

    result = plan_module_run(&plan_input, &g_app_runtime.plan_output);
    if (result != APP_OK) {
        /* 规划失败时不要保留旧路线 */
        (void)memset(&g_app_runtime.plan_output, 0, sizeof(g_app_runtime.plan_output));
        sim_module_reset(&g_app_runtime.sim_output);
        return result;
    }

    sim_module_reset(&g_app_runtime.sim_output);
    g_app_runtime.state = APP_STATE_PLAN_DONE;
    return APP_OK;
}

/* 仿真只沿已有航点移动，不重新规划路线 */
app_result_t app_core_start_sim(void)
{
    sim_input_t sim_input;
    app_result_t result;

    if ((g_app_runtime.state != APP_STATE_PLAN_DONE) &&
        (g_app_runtime.state != APP_STATE_SIM_RUNNING) &&
        (g_app_runtime.state != APP_STATE_SIM_DONE)) {
        return APP_ERR_STATE;
    }
    if (g_app_runtime.monitor_output.environment_status.wind_ok == 0U) {
        return APP_ERR_STATE;
    }

    /* 仿真只保存引用，避免复制大块路径和配药结果 */
    sim_input.plan = &g_app_runtime.plan_output;
    sim_input.mix_output = &g_app_runtime.mix_output;
    result = sim_module_start(&sim_input, &g_app_runtime.sim_output);
    if (result != APP_OK) {
        return result;
    }

    g_app_runtime.state = APP_STATE_SIM_RUNNING;
    return APP_OK;
}

/* 停止运行标志，保留最后画面 */
app_result_t app_core_stop_sim(void)
{
    app_result_t result;

    if (g_app_runtime.state != APP_STATE_SIM_RUNNING) {
        return APP_ERR_STATE;
    }

    result = sim_module_stop(&g_app_runtime.sim_output);
    if (result != APP_OK) {
        return result;
    }

    return APP_OK;
}

app_result_t app_core_trigger_recall(void)
{
    if (g_app_runtime.state != APP_STATE_SIM_RUNNING) {
        return APP_ERR_STATE;
    }
    return sim_module_trigger_recall(&g_app_runtime.sim_output);
}

app_result_t app_core_resume_from_refill(void)
{
    if (g_app_runtime.state != APP_STATE_SIM_RUNNING) {
        return APP_ERR_STATE;
    }
    return sim_module_resume_from_refill(&g_app_runtime.sim_output);
}

/* 当前流程状态 */
app_state_t app_core_get_state(void)
{
    return g_app_runtime.state;
}

/* 最近一次检测输出 */
const monitor_output_t *app_core_get_monitor_output(void)
{
    return &g_app_runtime.monitor_output;
}

/* 最近一次配药输出 */
const mix_output_t *app_core_get_mix_output(void)
{
    return &g_app_runtime.mix_output;
}

/* 最近一次路径规划输出 */
const plan_output_t *app_core_get_plan_output(void)
{
    return &g_app_runtime.plan_output;
}

/* 当前仿真输出 */
const sim_output_t *app_core_get_sim_output(void)
{
    return &g_app_runtime.sim_output;
}
