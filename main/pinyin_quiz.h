// main/pinyin_quiz.h —— 拼音易错题:题库类型 + 纯逻辑答题状态机。
//
// 本文件只依赖 C 标准库,不包含 LVGL / ESP-IDF 头文件:
// 状态机与题库都能在主机上直接编译测试(tests/test_pinyin_quiz.c)。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// 每题固定 4 个选项(题库编译期保证,主机测试会校验)。
#define PZ_OPTION_COUNT 4
// 一次练习最多容纳的题数。"全部混合"当前 48 题,留出余量。
#define PZ_MAX_QUESTIONS 64
// 题库总题数。写成编译期常量:错题本要按题目 ID 开定长数组。
#define PZ_BANK_SIZE 48
// 题型数量:"全部混合" + 6 个分类。写成编译期常量,界面才能用它开静态数组。
#define PZ_SET_COUNT 7

// 一道题:词语 + 4 个拼音选项(恰好一个正确) + 易错点解析。
typedef struct {
    const char *word;                        // 汉字词语,如 "朋友"
    const char *options[PZ_OPTION_COUNT];    // 4 个拼音选项,顺序即题库书写顺序
    const char *hint;                        // 解析:为什么这里会错
    uint8_t correct;                         // 正确项在 options[] 中的下标
} pz_item_t;

// 一个题型:在扁平题库 PZ_ITEMS[] 中的一段连续区间。
// 用偏移量而不是各自一份数组,"全部混合"就不用复制数据。
typedef struct {
    const char *name;   // 题型名,如 "前后鼻音"
    const char *tag;    // 2 字缩写,显示在题型选择左侧色块里
    uint8_t offset;     // 起始下标
    uint8_t count;      // 题数
} pz_set_t;

extern const pz_item_t PZ_ITEMS[PZ_BANK_SIZE];
extern const uint16_t PZ_ITEM_COUNT;
// PZ_SETS[0] 固定是 "全部混合",其后是各分类。
extern const pz_set_t PZ_SETS[PZ_SET_COUNT];
// 混合练习最多抽多少题(题量多时按此上限,避免一局太长)。
#define PZ_MIXED_LIMIT 20

typedef enum {
    PZ_PHASE_ASK = 0,   // 正在答题:上下移动光标
    PZ_PHASE_REVEAL,    // 已作答:显示对错与解析
    PZ_PHASE_DONE,      // 一轮结束:显示得分
} pz_phase_t;

typedef struct {
    uint8_t offset;                     // 本题组在 PZ_ITEMS[] 中的起点
    uint8_t total;                      // 本轮题数
    uint8_t index;                      // 当前第几题(0 基)
    uint8_t score;                      // 已答对题数
    uint8_t cursor;                     // 光标所在选项位置(0..3)
    uint8_t answer_slot;                // 本轮点下的位置(未答为 0xFF)
    uint8_t correct_slot;               // 正确项显示在哪个位置
    bool last_right;                    // 上一题是否答对
    pz_phase_t phase;
    uint16_t rng;                       // 线性同余随机源,保证主机测试可复现
    uint8_t order[PZ_MAX_QUESTIONS];    // 题目出场顺序(乱序)
    uint8_t perm[PZ_OPTION_COUNT];      // 选项显示顺序 perm[slot] = 题库下标
} pz_session_t;

// 开始一轮练习。set_index 必须 < PZ_SET_COUNT;seed 为 0 时按 1 处理。
void pz_session_start(pz_session_t *s, uint8_t set_index, uint16_t seed);

// 按指定的题目 ID 列表开一轮(错题本用)。ids 里的值必须 < PZ_BANK_SIZE。
// count 为 0 时直接进入 DONE,避免上层拿到一道不存在的题。
void pz_session_start_ids(pz_session_t *s, const uint8_t *ids, uint8_t count,
                          uint16_t seed);

// 移动光标。direction > 0 向下,< 0 向上;越界回绕。仅 ASK 有效。
void pz_session_move(pz_session_t *s, int8_t direction);

// 确认当前光标。仅 ASK 有效;返回是否答对。答完进入 REVEAL。
bool pz_session_confirm(pz_session_t *s);

// 从 REVEAL 前进:还有题则返回 true 并进入下一题的 ASK,否则进入 DONE 返回 false。
bool pz_session_advance(pz_session_t *s);

// 当前题目。DONE 阶段返回最后一题,便于上层复用。
const pz_item_t *pz_current_item(const pz_session_t *s);

// 当前题目在 PZ_ITEMS[] 中的全局 ID(0..PZ_BANK_SIZE-1)。错题本按它记账。
uint8_t pz_current_id(const pz_session_t *s);

// 第 slot 个位置要显示的拼音文本。
const char *pz_slot_text(const pz_session_t *s, uint8_t slot);

// 正确率 0..100(未答题时为 0)。
uint8_t pz_percent(const pz_session_t *s);

// 按正确率给一句评语。
const char *pz_verdict(uint8_t percent);
