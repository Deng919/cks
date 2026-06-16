#include "drivers.h"
#include "app_core.h"
#include "ui_module.h"
#include "lv_port_disp_template.h"
#include "lv_port_indev_template.h"


/*
 * 启动顺序：硬件和 LVGL 先起来，再初始化业务状态，最后创建 UI
 * 主循环只做调度，仿真业务按 10ms 节拍推进
 */
int main()
{
    uint8_t app_tick_divider;

    /* 板级外设初始化：时钟、SDRAM、LCD、触摸等都在 drivers 层处理 */
    sys_init();

    /* 先绑定 LVGL 的显示和输入设备，后面才能安全创建控件 */
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    /* UI 首次加载会读取 app_core 的默认状态 */
    app_core_init();
    ui_module_init();
    ui_module_load_home();
    app_tick_divider = 0U;

    while (1) {
        /* LVGL 约 2ms 跑一轮，业务 tick 每 5 轮合成 10ms */
        delay_us(2000);
        lv_timer_handler();

        app_tick_divider++;
        if (app_tick_divider >= 5U) {
            app_tick_divider = 0U;
            app_core_tick_10ms();
        }

        /* 页面刷新保持轻量，重对象只在切页或按钮动作里重建 */
        ui_module_refresh();
    }
}
