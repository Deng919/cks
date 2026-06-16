#ifndef UI_MODULE_H
#define UI_MODULE_H

/* 初始化 UI 上下文，不创建控件 */
void ui_module_init(void);

/* 创建主界面和页面容器 */
void ui_module_load_home(void);

/* 周期刷新当前页面的轻量显示 */
void ui_module_refresh(void);

#endif
