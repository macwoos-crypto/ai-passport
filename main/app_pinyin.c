// main/app_pinyin.c —— 「拼音易错题」界面与按键交互。
//
// 这是派生应用的独立界面:深色卡片 + 靛蓝强调色,首页是题型清单,答题页是
// 题目卡 + 四个拼音选项,答完切到解析页。没有复用参考 demo 的测试菜单外壳。
//
// 线程模型:LVGL 非线程安全。所有界面操作都在 app_pinyin_key() 里先拿
// bsp_lvgl_lock() 再做;电量刷新定时器跑在 LVGL 任务里,自带锁。
#include "app_pinyin.h"

#include <stdio.h>

#include "bsp_battery.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "pinyin_quiz.h"
#include "wrong_book.h"

LV_FONT_DECLARE(pz_font_16);
LV_FONT_DECLARE(pz_font_22);
LV_FONT_DECLARE(pz_font_32);

// 屏幕 240x320,四角由 BSP 按 30px 圆角遮黑,故内容左右各留 16px。
#define SCR_W        240
#define MARGIN       16
#define CONTENT_W    (SCR_W - MARGIN * 2)

// 深色底 + 卡片 + 靛蓝强调:文字对比度都在 7:1 以上,小屏阳光下也能读。
#define C_BG         0x0E1116
#define C_CARD       0x1A212B
#define C_LINE       0x2A3441
#define C_TEXT       0xE8EEF6
#define C_DIM        0x8A98AA
#define C_ACCENT     0x6366F1
#define C_ACCENT_IN  0x4338CA
#define C_OK         0x22C55E
#define C_BAD        0xEF4444

// 题型选择:8 行(全部混合 / 错题本 / 6 个分类)× 26px,从 y=68 排到 272。
#define MENU_ROWS      (PZ_SET_COUNT + 1)   // 6 个分类 + 全部混合 + 错题本
#define MENU_ROW_Y     68
#define MENU_ROW_STEP  26
#define MENU_ROW_H     22
// 菜单第 1 行是错题本,不是 pz_set_t;第 0 行是"全部混合",第 2 行起才是分类。
#define MENU_ROW_WRONG 1

// 答题页:4 个选项 × 39px,从 y=124 排到 275。
#define OPT_Y          124
#define OPT_STEP       39
#define OPT_H          34

typedef enum {
    UI_MENU = 0,
    UI_ASK,
    UI_REVEAL,
    UI_RESULT,
    UI_EMPTY,     // 错题本为空时的提示页
} ui_state_t;

static lv_obj_t *s_scr;
static lv_obj_t *s_bat;
static lv_timer_t *s_bat_timer;

static lv_obj_t *s_rows[MENU_ROWS];
static lv_obj_t *s_row_badge[MENU_ROWS];
static lv_obj_t *s_row_name[MENU_ROWS];
static lv_obj_t *s_row_count[MENU_ROWS];

static lv_obj_t *s_opts[PZ_OPTION_COUNT];
static lv_obj_t *s_opt_text[PZ_OPTION_COUNT];

static pz_session_t s_session;
static uint8_t s_row;          // 菜单当前选中行
static bool s_review;          // 本轮是不是错题本复习
static ui_state_t s_ui;

// ---------------------------------------------------------------- 基础构件

static lv_obj_t *make_screen(void) {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

static lv_obj_t *make_box(lv_obj_t *parent, int x, int y, int w, int h,
                          uint32_t bg, int radius) {
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, w, h);
    lv_obj_set_style_bg_color(box, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_radius(box, radius, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    return box;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                            int x, int y, int w, int h, lv_text_align_t align) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

// 自动尺寸 + 居中的标签:交给 LVGL 排版,纵向才是真正居中。
// (固定尺寸的标签,文字是从顶部开始画的,在卡片里会显得偏上。)
static lv_obj_t *make_center_label(lv_obj_t *parent, const lv_font_t *font,
                                   uint32_t color, const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return label;
}

static void update_battery(void) {
    if (s_bat == NULL) return;
    int soc = bsp_battery_soc();
    if (soc < 0) {
        // 读不到电量就显示占位,不要画一个假的 0%。
        lv_label_set_text(s_bat, "--");
        lv_obj_set_style_text_color(s_bat, lv_color_hex(C_DIM), 0);
        return;
    }
    lv_label_set_text_fmt(s_bat, "%d%%", soc);
    lv_obj_set_style_text_color(s_bat,
        lv_color_hex(soc < 20 ? C_BAD : C_DIM), 0);
}

// lv_timer 回调签名带参数,单独包一层,不要强转函数指针。
static void bat_tick(lv_timer_t *timer) {
    (void)timer;
    update_battery();
}

// 右下角的电量读数。所有页面都放,失败时自动退化。
static void add_battery(lv_obj_t *scr, int y) {
    s_bat = make_label(scr, &pz_font_16, C_DIM, 150, y, 74, 18, LV_TEXT_ALIGN_RIGHT);
    update_battery();
}

// 清空指向旧屏的控件引用,并**先删掉旧屏**。
//
// 顺序很关键:如果先建新屏再删旧屏,峰值内存是"两屏同时存在"。实测菜单页
// (8 行 × 5 个对象 ≈ 44 个控件)与答题页共存时,24KB 的 LVGL 池只剩 1236 字节,
// 最大可用块仅 1224 —— 到了这个水位,多一个标签或分片再多一点就会分配失败,
// 表现为界面卡死。
//
// 先删后建后,峰值等于"较大的那一屏",内存立刻宽松。中间不会有白屏:
// 调用方此时持有 LVGL 锁,渲染任务拿不到锁,不会在删与建之间插进来画一帧。
static void reset_refs(void) {
    if (s_scr != NULL) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
    }
    s_bat = NULL;
    for (uint8_t i = 0; i < MENU_ROWS; i++) {
        s_rows[i] = NULL;
        s_row_badge[i] = NULL;
        s_row_name[i] = NULL;
        s_row_count[i] = NULL;
    }
    for (uint8_t i = 0; i < PZ_OPTION_COUNT; i++) {
        s_opts[i] = NULL;
        s_opt_text[i] = NULL;
    }
}

// 载入新屏。旧屏已在 reset_refs() 里释放,这里只负责切过去。
// 平时不打日志;只在可用块偏小时告警 —— 内存用穿的表现是"页面建了一半然后卡死",
// 与其事后靠看门狗寄存器猜,不如在水位逼近时先喊一声。
static void ui_load(lv_obj_t *scr) {
    s_scr = scr;
    lv_screen_load(scr);

    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    if (mon.free_biggest_size < 3072) {
        ESP_LOGW("uimem", "LVGL 内存紧张:剩余 %u,最大可用块 %u (已用 %d%%)",
                 (unsigned)mon.free_size, (unsigned)mon.free_biggest_size,
                 (int)mon.used_pct);
    }
}

// ---------------------------------------------------------------- 题型选择页

// 菜单行 → 分类下标。第 1 行是错题本,不是分类;第 0 行是"全部混合"(分类 0)。
static uint8_t row_to_set(uint8_t row) {
    return (uint8_t)(row == 0 ? 0 : row - 1);
}

static bool row_is_review(uint8_t row) {
    return row == MENU_ROW_WRONG;
}

static const char *row_tag(uint8_t row) {
    return row_is_review(row) ? "错题" : PZ_SETS[row_to_set(row)].tag;
}

static const char *row_name(uint8_t row) {
    return row_is_review(row) ? "错题本" : PZ_SETS[row_to_set(row)].name;
}

// 行右侧要显示的题数。「全部混合」按实际上限显示,错题本按当前错题数显示。
static void row_count_text(uint8_t row, char *buf, size_t len) {
    if (row_is_review(row)) {
        uint16_t n = wb_count();
        if (n == 0) snprintf(buf, len, "空");
        else snprintf(buf, len, "%d 题", (int)n);
        return;
    }
    uint8_t set = row_to_set(row);
    uint8_t size = (set == 0 && PZ_SETS[0].count > PZ_MIXED_LIMIT)
                   ? PZ_MIXED_LIMIT : PZ_SETS[set].count;
    snprintf(buf, len, "%d 题", (int)size);
}

static void refresh_menu(void) {
    for (uint8_t i = 0; i < MENU_ROWS; i++) {
        if (s_rows[i] == NULL) continue;
        bool sel = (i == s_row);
        lv_obj_set_style_bg_color(s_rows[i],
            lv_color_hex(sel ? C_ACCENT : C_CARD), 0);
        lv_obj_set_style_bg_color(s_row_badge[i],
            lv_color_hex(sel ? C_ACCENT_IN : C_LINE), 0);
        lv_obj_set_style_text_color(s_row_badge[i],
            lv_color_hex(sel ? 0xFFFFFF : C_DIM), 0);
        lv_obj_set_style_text_color(s_row_name[i],
            lv_color_hex(sel ? 0xFFFFFF : C_TEXT), 0);
        lv_obj_set_style_text_color(s_row_count[i],
            lv_color_hex(sel ? 0xE0E7FF : C_DIM), 0);
    }
}

static void build_menu(void) {
    reset_refs();
    lv_obj_t *scr = make_screen();

    lv_obj_t *title = make_label(scr, &pz_font_22, C_TEXT,
                                 MARGIN, 12, 150, 28, LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(title, "拼音易错题");
    add_battery(scr, 16);

    lv_obj_t *sub = make_label(scr, &pz_font_16, C_DIM,
                               MARGIN, 42, CONTENT_W, 18, LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(sub, "只练最容易写错的音");

    for (uint8_t i = 0; i < MENU_ROWS; i++) {
        int y = MENU_ROW_Y + i * MENU_ROW_STEP;
        s_rows[i] = make_box(scr, MARGIN, y, CONTENT_W, MENU_ROW_H, C_CARD, 8);

        s_row_badge[i] = make_box(s_rows[i], 4, 2, 30, 18, C_LINE, 6);
        make_center_label(s_row_badge[i], &pz_font_16, C_DIM, row_tag(i));

        s_row_name[i] = make_label(s_rows[i], &pz_font_16, C_TEXT,
                                   44, 2, 100, 18, LV_TEXT_ALIGN_LEFT);
        lv_label_set_text(s_row_name[i], row_name(i));

        s_row_count[i] = make_label(s_rows[i], &pz_font_16, C_DIM,
                                    0, 2, 44, 18, LV_TEXT_ALIGN_RIGHT);
        lv_obj_align(s_row_count[i], LV_ALIGN_RIGHT_MID, -8, 0);
        char count[12];
        row_count_text(i, count, sizeof(count));
        lv_label_set_text(s_row_count[i], count);
    }

    lv_obj_t *foot = make_label(scr, &pz_font_16, C_DIM,
                                MARGIN, 280, CONTENT_W, 18, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(foot, "上/下 选择    OK 开始");

    refresh_menu();
    ui_load(scr);
}

// ---------------------------------------------------------------- 答题页

static void refresh_options(void) {
    for (uint8_t i = 0; i < PZ_OPTION_COUNT; i++) {
        if (s_opts[i] == NULL) continue;
        bool sel = (i == s_session.cursor);
        lv_obj_set_style_bg_color(s_opts[i],
            lv_color_hex(sel ? C_ACCENT : C_CARD), 0);
        lv_obj_set_style_text_color(s_opt_text[i],
            lv_color_hex(sel ? 0xFFFFFF : C_TEXT), 0);
    }
}

static void build_question(void) {
    reset_refs();
    lv_obj_t *scr = make_screen();
    const pz_item_t *item = pz_current_item(&s_session);

    lv_obj_t *head = make_label(scr, &pz_font_16, C_DIM,
                                MARGIN, 12, 120, 18, LV_TEXT_ALIGN_LEFT);
    lv_label_set_text_fmt(head, "第 %d/%d 题",
                          (int)s_session.index + 1, (int)s_session.total);
    add_battery(scr, 12);

    // 进度条:已答完的题数占比,给"还剩几道"的空间感。
    make_box(scr, MARGIN, 36, CONTENT_W, 5, C_LINE, 2);
    int fill_w = (CONTENT_W * ((int)s_session.index + 1)) / (int)s_session.total;
    if (fill_w < 5) fill_w = 5;
    make_box(scr, MARGIN, 36, fill_w, 5, C_ACCENT, 2);

    // 词语卡是唯一的大字区。标签挂到卡片上再居中,挂到屏幕上会跑到整屏正中。
    lv_obj_t *card = make_box(scr, MARGIN, 52, CONTENT_W, 64, C_CARD, 12);
    lv_obj_t *word = make_center_label(card, &pz_font_32, C_TEXT, item->word);
    lv_obj_set_style_text_letter_space(word, 2, 0);

    for (uint8_t i = 0; i < PZ_OPTION_COUNT; i++) {
        int y = OPT_Y + i * OPT_STEP;
        s_opts[i] = make_box(scr, MARGIN, y, CONTENT_W, OPT_H, C_CARD, 10);
        s_opt_text[i] = make_center_label(s_opts[i], &pz_font_22, C_TEXT,
                                          pz_slot_text(&s_session, i));
    }

    lv_obj_t *foot = make_label(scr, &pz_font_16, C_DIM,
                                MARGIN, 282, CONTENT_W, 18, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(foot, "上/下 选择    OK 确认");

    refresh_options();
    ui_load(scr);
}

// ---------------------------------------------------------------- 解析页

static void build_reveal(void) {
    reset_refs();
    lv_obj_t *scr = make_screen();
    const pz_item_t *item = pz_current_item(&s_session);
    bool right = s_session.last_right;

    lv_obj_t *verdict = make_label(scr, &pz_font_22, right ? C_OK : C_BAD,
                                   MARGIN, 14, CONTENT_W, 26, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(verdict, right ? "✓ 正确" : "× 答错了");
    add_battery(scr, 14);

    lv_obj_t *word = make_label(scr, &pz_font_22, C_TEXT,
                                MARGIN, 50, CONTENT_W, 26, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text_fmt(word, "%s", item->word);

    // 正确答案用强调色 + 最大字号,一屏里最容易看到的就是它。
    lv_obj_t *answer = make_label(scr, &pz_font_32, C_ACCENT,
                                  MARGIN, 80, CONTENT_W, 40, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(answer, item->options[item->correct]);

    make_box(scr, 60, 128, CONTENT_W - 88, 1, C_LINE, 0);

    lv_obj_t *hint = make_label(scr, &pz_font_16, C_DIM,
                                24, 140, CONTENT_W - 16, 100, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(hint, item->hint);

    if (!right) {
        // 只答错时回显选了什么,方便对照"错在哪个音"。
        lv_obj_t *pick = make_label(scr, &pz_font_16, C_BAD,
                                    MARGIN, 248, CONTENT_W, 18, LV_TEXT_ALIGN_CENTER);
        lv_label_set_text_fmt(pick, "你选的是 %s", pz_slot_text(&s_session, s_session.answer_slot));
    }

    lv_obj_t *foot = make_label(scr, &pz_font_16, C_DIM,
                                MARGIN, 282, CONTENT_W, 18, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(foot, "OK 下一题    长按 OK 退出");

    ui_load(scr);
}

// ---------------------------------------------------------------- 结算页

static void build_result(void) {
    reset_refs();
    lv_obj_t *scr = make_screen();
    uint8_t pct = pz_percent(&s_session);

    lv_obj_t *head = make_label(scr, &pz_font_22, C_DIM,
                                MARGIN, 26, CONTENT_W, 26, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(head, s_review ? "错题本复习" : "本轮结束");
    add_battery(scr, 26);

    lv_obj_t *score = make_label(scr, &pz_font_32, C_TEXT,
                                 MARGIN, 60, CONTENT_W, 40, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text_fmt(score, "%d / %d", (int)s_session.score, (int)s_session.total);

    lv_obj_t *rate = make_label(scr, &pz_font_22, C_ACCENT,
                                MARGIN, 108, CONTENT_W, 26, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text_fmt(rate, "正确率 %d%%", pct);

    // 五颗星按正确率给,比单纯看百分比更有反馈感。
    static const char *const STARS[] = {
        "★", "★★", "★★★", "★★★★", "★★★★★",
    };
    int stars = (pct >= 95) ? 5 : (pct >= 80) ? 4 : (pct >= 60) ? 3
              : (pct >= 40) ? 2 : 1;
    lv_obj_t *star = make_label(scr, &pz_font_22, 0xF5C044,
                                MARGIN, 146, CONTENT_W, 26, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(star, STARS[stars - 1]);

    lv_obj_t *verdict = make_label(scr, &pz_font_16, C_DIM,
                                   20, 190, CONTENT_W - 8, 44, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(verdict, pz_verdict(pct));

    lv_obj_t *again = make_label(scr, &pz_font_16, C_TEXT,
                                 MARGIN, 250, CONTENT_W, 18, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(again, "OK 再来一轮");

    lv_obj_t *back = make_label(scr, &pz_font_16, C_DIM,
                                MARGIN, 278, CONTENT_W, 18, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(back, "长按 OK 返回题型");

    ui_load(scr);
}

// ---------------------------------------------------------------- 空错题本提示

static void build_empty(void) {
    reset_refs();
    lv_obj_t *scr = make_screen();

    lv_obj_t *head = make_label(scr, &pz_font_22, C_TEXT,
                                MARGIN, 92, CONTENT_W, 30, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(head, "错题本是空的");
    add_battery(scr, 16);

    lv_obj_t *hint = make_label(scr, &pz_font_16, C_DIM,
                                24, 136, CONTENT_W - 16, 64, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(hint, "先去练一轮，做错的题会自动进来。\n在错题本里做对一次，就少一道。");

    lv_obj_t *foot = make_label(scr, &pz_font_16, C_DIM,
                                MARGIN, 280, CONTENT_W, 18, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(foot, "OK 返回");

    ui_load(scr);
}

// ---------------------------------------------------------------- 流程

static void start_round(void) {
    // 每轮换随机源,题序与选项顺序都不会和上一轮一样。
    uint16_t seed = (uint16_t)(esp_timer_get_time() & 0xFFFF);

    if (row_is_review(s_row)) {
        uint8_t ids[PZ_BANK_SIZE];
        uint8_t n = wb_ids(ids, (uint8_t)sizeof(ids));
        if (n == 0) {
            s_ui = UI_EMPTY;
            build_empty();
            return;
        }
        s_review = true;
        pz_session_start_ids(&s_session, ids, n, seed);
    } else {
        s_review = false;
        pz_session_start(&s_session, row_to_set(s_row), seed);
    }

    s_ui = UI_ASK;
    build_question();
}

static void go_menu(void) {
    s_ui = UI_MENU;
    build_menu();
}

void app_pinyin_enter(void) {
    s_row = 0;
    s_review = false;
    s_ui = UI_MENU;
    build_menu();

    if (s_bat_timer == NULL) {
        // 跑在 LVGL 任务里,已经持锁;20 秒一次足够,不必每帧读 I2C。
        s_bat_timer = lv_timer_create(bat_tick, 20000, NULL);
    }
}

void app_pinyin_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    // PRESS/RELEASE/DOUBLE 在本应用里不做动作:只用单击和长按,避免误触。
    if (ev != BSP_BTN_CLICK && ev != BSP_BTN_LONG) return;
    if (!bsp_lvgl_lock(300)) return;

    switch (s_ui) {
    case UI_MENU:
        if (ev == BSP_BTN_CLICK && btn == BSP_BTN_UP) {
            s_row = (uint8_t)((s_row + MENU_ROWS - 1) % MENU_ROWS);
            refresh_menu();
        } else if (ev == BSP_BTN_CLICK && btn == BSP_BTN_DOWN) {
            s_row = (uint8_t)((s_row + 1) % MENU_ROWS);
            refresh_menu();
        } else if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
            start_round();
        }
        break;

    case UI_ASK:
        if (ev == BSP_BTN_CLICK && btn == BSP_BTN_UP) {
            pz_session_move(&s_session, -1);
            refresh_options();
        } else if (ev == BSP_BTN_CLICK && btn == BSP_BTN_DOWN) {
            pz_session_move(&s_session, 1);
            refresh_options();
        } else if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
            const bool right = pz_session_confirm(&s_session);
            // 就地记账:答错进错题本,错题本里答对则递减。写在交卷之后、
            // 画解析页之前,解析页就能直接反映这一题的归属。
            wb_note(pz_current_id(&s_session), right, s_review);
            s_ui = UI_REVEAL;
            build_reveal();
        } else if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
            go_menu();
        }
        break;

    case UI_REVEAL:
        if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
            if (pz_session_advance(&s_session)) {
                s_ui = UI_ASK;
                build_question();
            } else {
                s_ui = UI_RESULT;
                build_result();
            }
        } else if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
            go_menu();
        }
        break;

    case UI_RESULT:
        if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
            start_round();
        } else if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
            go_menu();
        }
        break;

    case UI_EMPTY:
        // 空错题本只有一个出口,两种按法都回菜单。
        go_menu();
        break;

    default:
        go_menu();
        break;
    }

    bsp_lvgl_unlock();
}
