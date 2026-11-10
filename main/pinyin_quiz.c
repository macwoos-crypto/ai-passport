// main/pinyin_quiz.c —— 答题状态机。纯 C,可在主机上编译运行(tests/test_pinyin_quiz.c)。
//
// 设计要点:
//   * 题目顺序、选项顺序都用同一个 16 位线性同余随机源打乱,seed 由上层传入,
//     因此主机测试可以用固定 seed 断言确定的顺序。
//   * 题库是扁平数组,题型只是区间;混合练习取前 PZ_MIXED_LIMIT 道,不复制数据。
#include <stddef.h>

#include "pinyin_quiz.h"

static uint16_t pz_rand(pz_session_t *s) {
    // 25173/13849 是 ZX Spectrum 用过的参数,mcu 上足够打散 48 道题。
    s->rng = (uint16_t)(s->rng * 25173u + 13849u);
    return s->rng;
}

const pz_item_t *pz_current_item(const pz_session_t *s) {
    // 空题组(例如错题本已清空)没有"当前题",返回首题占位,不能让下标回绕成 255。
    if (s->total == 0) return &PZ_ITEMS[0];
    uint8_t slot = (s->index < s->total) ? s->index : (uint8_t)(s->total - 1);
    return &PZ_ITEMS[s->offset + s->order[slot]];
}

uint8_t pz_current_id(const pz_session_t *s) {
    if (s->total == 0) return 0;
    uint8_t slot = (s->index < s->total) ? s->index : (uint8_t)(s->total - 1);
    return (uint8_t)(s->offset + s->order[slot]);
}

// 抽题后确定选项显示顺序:perm[slot] = 题库里的选项下标。
static void load_question(pz_session_t *s) {
    for (uint8_t i = 0; i < PZ_OPTION_COUNT; i++) s->perm[i] = i;
    for (uint8_t i = PZ_OPTION_COUNT; i > 1; i--) {
        uint8_t j = (uint8_t)(pz_rand(s) % i);
        uint8_t t = s->perm[i - 1];
        s->perm[i - 1] = s->perm[j];
        s->perm[j] = t;
    }

    const pz_item_t *item = pz_current_item(s);
    s->correct_slot = 0;
    for (uint8_t i = 0; i < PZ_OPTION_COUNT; i++) {
        if (s->perm[i] == item->correct) s->correct_slot = i;
    }
    s->cursor = 0;
    s->answer_slot = 0xFF;
}

// Fisher-Yates:只打乱前 n 项,越界部分保持 0(整个结构体在 begin 时已清零)。
static void shuffle_order(pz_session_t *s, uint8_t n) {
    for (uint8_t i = n; i > 1; i--) {
        uint8_t j = (uint8_t)(pz_rand(s) % i);
        uint8_t t = s->order[i - 1];
        s->order[i - 1] = s->order[j];
        s->order[j] = t;
    }
}

// 两种入口共用的收尾:打乱 order[0..pool-1]、只启用前 total 道。
// 调用方负责先把 order 填好(分类/混合填 0..pool-1,错题本填题目 ID)。
static void session_tail(pz_session_t *s, uint8_t offset, uint8_t pool,
                         uint8_t total, uint16_t seed) {
    s->offset = offset;
    s->total = total;
    s->score = 0;
    s->index = 0;
    s->last_right = false;
    s->rng = seed ? seed : 1;
    shuffle_order(s, pool);
    if (total == 0) {
        s->phase = PZ_PHASE_DONE;
        return;
    }
    s->phase = PZ_PHASE_ASK;
    load_question(s);
}

void pz_session_start(pz_session_t *s, uint8_t set_index, uint16_t seed) {
    if (set_index >= PZ_SET_COUNT) set_index = 0;
    const pz_set_t *set = &PZ_SETS[set_index];

    uint8_t pool = set->count;
    if (pool > PZ_MAX_QUESTIONS) pool = PZ_MAX_QUESTIONS;
    uint8_t total = pool;
    if (set_index == 0 && total > PZ_MIXED_LIMIT) total = PZ_MIXED_LIMIT;

    // 整体清零:order[] 只用到前 pool 项,残留的旧值会让相同 seed 的比较失真。
    *s = (pz_session_t){ 0 };
    // 填满整个题库池再打乱,"全部混合"才是从 48 道里抽 20 道,而不是永远取前 20 道。
    for (uint8_t i = 0; i < pool; i++) s->order[i] = i;
    session_tail(s, set->offset, pool, total, seed);
}

void pz_session_start_ids(pz_session_t *s, const uint8_t *ids, uint8_t count,
                          uint16_t seed) {
    uint8_t total = count;
    if (total > PZ_BANK_SIZE) total = PZ_BANK_SIZE;
    if (total > PZ_MAX_QUESTIONS) total = PZ_MAX_QUESTIONS;

    *s = (pz_session_t){ 0 };
    // order[] 里存的是全局题目 ID,offset 固定 0,PZ_ITEMS[offset + order[i]] 依然成立。
    for (uint8_t i = 0; i < total; i++) {
        s->order[i] = (ids != NULL && ids[i] < PZ_BANK_SIZE) ? ids[i] : 0;
    }
    // 错题本里的题全部要练,不做抽样;pool 就是 total,只把顺序打乱。
    session_tail(s, 0, total, total, seed);
}

void pz_session_move(pz_session_t *s, int8_t direction) {
    if (s->phase != PZ_PHASE_ASK || direction == 0) return;
    int next = (int)s->cursor + (direction > 0 ? 1 : -1);
    if (next < 0) next = PZ_OPTION_COUNT - 1;
    if (next >= PZ_OPTION_COUNT) next = 0;
    s->cursor = (uint8_t)next;
}

bool pz_session_confirm(pz_session_t *s) {
    if (s->phase != PZ_PHASE_ASK) return false;
    s->answer_slot = s->cursor;
    s->last_right = (s->cursor == s->correct_slot);
    if (s->last_right && s->score < 0xFF) s->score++;
    s->phase = PZ_PHASE_REVEAL;
    return s->last_right;
}

bool pz_session_advance(pz_session_t *s) {
    if (s->phase != PZ_PHASE_REVEAL) return false;
    if ((uint8_t)(s->index + 1) >= s->total) {
        s->index = (uint8_t)(s->total - 1);
        s->phase = PZ_PHASE_DONE;
        return false;
    }
    s->index++;
    load_question(s);
    s->phase = PZ_PHASE_ASK;
    return true;
}

const char *pz_slot_text(const pz_session_t *s, uint8_t slot) {
    if (slot >= PZ_OPTION_COUNT) return "";
    return pz_current_item(s)->options[s->perm[slot]];
}

uint8_t pz_percent(const pz_session_t *s) {
    if (s->total == 0) return 0;
    return (uint8_t)(((uint16_t)s->score * 100u) / s->total);
}

const char *pz_verdict(uint8_t percent) {
    if (percent >= 90) return "这些音你全拿住了";
    if (percent >= 70) return "不错，还剩几个坑要填";
    if (percent >= 40) return "半对半错，易错点得再刷一轮";
    return "先记口诀，再来一轮一定进步";
}
