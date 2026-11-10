// main/wrong_book_logic.c —— 错题本的纯逻辑部分。
//
// 这里不 include 任何 ESP-IDF / LVGL 头文件,主机测试可以直接编译本文件。
// NVS 读写见 wrong_book.c。
#include <string.h>

#include "wrong_book.h"

void wb_reset(wb_book_t *book) {
    if (book == NULL) return;
    memset(book, 0, sizeof(*book));
}

void wb_mark_wrong(wb_book_t *book, uint8_t id) {
    if (book == NULL || id >= PZ_BANK_SIZE) return;
    if (book->counts[id] < WB_MAX_LEVEL) book->counts[id]++;
}

void wb_mark_right(wb_book_t *book, uint8_t id) {
    if (book == NULL || id >= PZ_BANK_SIZE) return;
    if (book->counts[id] > 0) book->counts[id]--;
}

uint8_t wb_level(const wb_book_t *book, uint8_t id) {
    if (book == NULL || id >= PZ_BANK_SIZE) return 0;
    return book->counts[id];
}

uint16_t wb_total(const wb_book_t *book) {
    if (book == NULL) return 0;
    uint16_t n = 0;
    for (uint8_t i = 0; i < PZ_BANK_SIZE; i++) {
        if (book->counts[i] > 0) n++;
    }
    return n;
}

uint8_t wb_collect(const wb_book_t *book, uint8_t *out, uint8_t max) {
    if (book == NULL || out == NULL || max == 0) return 0;

    // 选择排序:每轮挑出剩余题里错次最高的,错次相同则按 ID 升序。
    // 题量只有 48,写个 O(n^2) 比引入排序函数更直观,也省一个临时数组。
    bool used[PZ_BANK_SIZE];
    memset(used, 0, sizeof(used));

    uint8_t n = 0;
    while (n < max) {
        int best = -1;
        for (uint8_t i = 0; i < PZ_BANK_SIZE; i++) {
            if (used[i] || book->counts[i] == 0) continue;
            if (best < 0 || book->counts[i] > book->counts[best]) best = i;
        }
        if (best < 0) break;
        used[best] = true;
        out[n++] = (uint8_t)best;
    }
    return n;
}
