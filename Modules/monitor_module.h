#ifndef MONITOR_MODULE_H
#define MONITOR_MODULE_H

#include "app_types.h"

/*
 * 农田检测模块
 * 只生成地块状态、统计和作业 region，不计算药量和路径
 */

/* 初始化自动检测备用随机种子 */
void monitor_module_init(void);

/* 填充默认检测输入；input 为空时直接返回 */
void monitor_module_fill_default_input(monitor_input_t *input);

/* 完整重建检测输出 */
app_result_t monitor_module_run(const monitor_input_t *input, monitor_output_t *output);

/* 在已有检测输出上改一块地，并重算 stats 和 region */
app_result_t monitor_module_apply_manual(const monitor_input_t *input, monitor_output_t *output);

#endif
