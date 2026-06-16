#ifndef MIX_MODULE_H
#define MIX_MODULE_H

#include "app_types.h"

/*
 * 配药模块
 * 根据检测 region 算液量并按药箱容量拆桶，不生成航点
 */

/* 当前无内部状态，保留生命周期接口 */
void mix_module_init(void);

/* 执行配药计算和拆桶 */
app_result_t mix_module_run(const monitor_output_t *monitor, const mix_input_t *input, mix_output_t *output);

#endif
