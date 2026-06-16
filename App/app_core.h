#ifndef APP_CORE_H
#define APP_CORE_H

#include "app_types.h"

/* app_core 负责串起 monitor -> mix -> plan -> sim 的主流程 */

/* 初始化业务运行态和各模块 */
void app_core_init(void);

/* 10ms 业务节拍，目前只推进运行中的仿真 */
void app_core_tick_10ms(void);

/* 重新生成检测结果；成功后会清掉下游结果 */
app_result_t app_core_run_monitor(const monitor_input_t *input);

/* 在现有检测结果上修改单块地，并重算统计和 region */
app_result_t app_core_apply_manual_monitor(const monitor_input_t *input);

/* 根据检测结果和药箱容量做配药拆桶 */
app_result_t app_core_run_mix_with_input(const mix_input_t *input);

/* 根据拆桶结果生成航点路径 */
app_result_t app_core_run_plan(void);

/* 从当前规划路径启动仿真 */
app_result_t app_core_start_sim(void);

/* 停止仿真，但保留最后一次路径和进度 */
app_result_t app_core_stop_sim(void);

/* 主动召回返航 */
app_result_t app_core_trigger_recall(void);

/* 补药处一键起飞重返断点 */
app_result_t app_core_resume_from_refill(void);

/* 当前流程状态 */
app_state_t app_core_get_state(void);

/* 最近一次检测输出，只读 */
const monitor_output_t *app_core_get_monitor_output(void);

/* 最近一次配药输出，只读 */
const mix_output_t *app_core_get_mix_output(void);

/* 最近一次规划输出，只读 */
const plan_output_t *app_core_get_plan_output(void);

/* 当前仿真输出，只读 */
const sim_output_t *app_core_get_sim_output(void);

#endif
