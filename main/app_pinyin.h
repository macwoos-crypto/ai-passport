// main/app_pinyin.h —— 拼音易错题应用的对外入口。
//
// 这里只暴露生命周期与按键入口;界面、状态机装配都在 app_pinyin.c 内。
#pragma once

#include "bsp_button.h"

// 首次进入:创建题型选择首页并载入。必须在 LVGL 就绪后调用一次。
void app_pinyin_enter(void);

// 按键分发。调用方无需持有 LVGL 锁,函数内部自行加锁。
void app_pinyin_key(bsp_btn_t btn, bsp_btn_ev_t ev);
