// tests/test_wrong_book.c —— 错题本纯逻辑(不含 NVS)。
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "wrong_book.h"

static void test_empty(void) {
    wb_book_t book;
    wb_reset(&book);
    assert(wb_total(&book) == 0);
    for (uint8_t i = 0; i < PZ_BANK_SIZE; i++) assert(wb_level(&book, i) == 0);
}

static void test_mark_wrong_accumulates(void) {
    wb_book_t book;
    wb_reset(&book);

    wb_mark_wrong(&book, 5);
    assert(wb_level(&book, 5) == 1);
    assert(wb_total(&book) == 1);

    wb_mark_wrong(&book, 5);
    assert(wb_level(&book, 5) == 2);
    // 同一个题错多次,错题本里仍然只算一道。
    assert(wb_total(&book) == 1);

    // 封顶:超过 WB_MAX_LEVEL 不再累加,避免 uint8 回绕。
    for (int i = 0; i < 50; i++) wb_mark_wrong(&book, 5);
    assert(wb_level(&book, 5) == WB_MAX_LEVEL);
}

static void test_mark_right_decays(void) {
    wb_book_t book;
    wb_reset(&book);

    wb_mark_wrong(&book, 9);
    wb_mark_wrong(&book, 9);
    wb_mark_wrong(&book, 9);
    assert(wb_level(&book, 9) == 3);

    wb_mark_right(&book, 9);
    assert(wb_level(&book, 9) == 2);

    wb_mark_right(&book, 9);
    wb_mark_right(&book, 9);
    assert(wb_level(&book, 9) == 0);
    assert(wb_total(&book) == 0);

    // 已在 0 上再答对,不能减成 255。
    wb_mark_right(&book, 9);
    assert(wb_level(&book, 9) == 0);
    assert(wb_total(&book) == 0);
}

static void test_out_of_range(void) {
    wb_book_t book;
    wb_reset(&book);

    wb_mark_wrong(&book, PZ_BANK_SIZE);
    wb_mark_wrong(&book, 255);
    assert(wb_total(&book) == 0);
    assert(wb_level(&book, PZ_BANK_SIZE) == 0);

    // 空指针不应该崩,也不应该写坏内存。
    wb_reset(NULL);
    wb_mark_wrong(NULL, 0);
    wb_mark_right(NULL, 0);
    assert(wb_level(NULL, 0) == 0);
    assert(wb_total(NULL) == 0);

    uint8_t out[4];
    assert(wb_collect(NULL, out, 4) == 0);
    wb_book_t b2;
    wb_reset(&b2);
    assert(wb_collect(&b2, NULL, 4) == 0);
    assert(wb_collect(&b2, out, 0) == 0);
}

static void test_collect_order(void) {
    wb_book_t book;
    wb_reset(&book);

    // 错次:2 号 1 次、7 号 3 次、20 号 2 次 → 期望先 7,再 20,最后 2。
    wb_mark_wrong(&book, 2);
    for (int i = 0; i < 3; i++) wb_mark_wrong(&book, 7);
    wb_mark_wrong(&book, 20);
    wb_mark_wrong(&book, 20);

    uint8_t out[PZ_BANK_SIZE];
    uint8_t n = wb_collect(&book, out, (uint8_t)sizeof(out));
    assert(n == 3);
    assert(out[0] == 7);
    assert(out[1] == 20);
    assert(out[2] == 2);

    // max 限制生效,且不会超出调用方缓冲。
    n = wb_collect(&book, out, 1);
    assert(n == 1);
    assert(out[0] == 7);
}

static void test_collect_tie_breaks_by_id(void) {
    wb_book_t book;
    wb_reset(&book);
    wb_mark_wrong(&book, 30);
    wb_mark_wrong(&book, 4);
    wb_mark_wrong(&book, 11);

    uint8_t out[PZ_BANK_SIZE];
    uint8_t n = wb_collect(&book, out, (uint8_t)sizeof(out));
    assert(n == 3);
    // 错次相同时按 ID 升序,顺序稳定可复现。
    assert(out[0] == 4);
    assert(out[1] == 11);
    assert(out[2] == 30);
}

int main(void) {
    test_empty();
    test_mark_wrong_accumulates();
    test_mark_right_decays();
    test_out_of_range();
    test_collect_order();
    test_collect_tie_breaks_by_id();
    printf("test_wrong_book: PASS\n");
    return 0;
}
