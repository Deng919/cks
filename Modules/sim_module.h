#ifndef SIM_MODULE_H
#define SIM_MODULE_H

#include "app_types.h"

/*
 * 飞行仿真模块
 * 沿 plan_output_t 的航点推进，并按 mix_output_t 估算剩余液量
 */

typedef struct
{
    /* 航点、路径距离和每桶任务范围 */
    const plan_output_t *plan;

    /* 每桶初始药液量 */
    const mix_output_t *mix_output;
} sim_input_t;

/* 初始化内部引用、计时和默认倍速 */
void sim_module_init(void);

/* 启动仿真；只保存 plan/mix 引用，不复制大数组 */
app_result_t sim_module_start(const sim_input_t *input, sim_output_t *output);

/* 按 10ms 节拍刷新位置、进度、覆盖率和剩余液量 */
app_result_t sim_module_tick_10ms(sim_output_t *output);

/* 停止 running 标志，保留最后画面 */
app_result_t sim_module_stop(sim_output_t *output);

/* 清空输出并重置内部累计时间；output 允许为空 */
void sim_module_reset(sim_output_t *output);

/* 当前仿真倍速 */
uint16_t sim_module_get_time_scale(void);

/* 切换到下一个预设倍速 */
uint16_t sim_module_cycle_time_scale(void);

/* 主动触发手动召回 */
app_result_t sim_module_trigger_recall(sim_output_t *output);

/* 补给点起飞/继续 */
app_result_t sim_module_resume_from_refill(sim_output_t *output);

#endif

