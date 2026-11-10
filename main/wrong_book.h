// main/wrong_book.h —— 错题本:纯逻辑数据结构 + NVS 持久化接口。
//
// 两层分开是为了可测试:
//   * wb_* (wb_book_t*)  —— 纯 C,不碰 NVS,主机测试直接跑(tests/test_wrong_book.c)
//   * wb_init / wb_note / wb_count / wb_ids —— 单例 + NVS 读写,只在固件里用
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pinyin_quiz.h"

// 单题最多累计的"错次"。封顶有两个作用:uint8 不溢出,而且错 9 次和错 12 次
// 对复习优先级没有区别。
#define WB_MAX_LEVEL 9

typedef struct {
    uint8_t counts[PZ_BANK_SIZE];   // 0 = 不在此本上;>0 = 还需复习的强度
} wb_book_t;

// ---------------------------------------------------------------- 纯逻辑

void wb_reset(wb_book_t *book);

// 答错:错次 +1(封顶 WB_MAX_LEVEL),该题进入错题本。
void wb_mark_wrong(wb_book_t *book, uint8_t id);

// 在错题本里答对:错次 -1,减到 0 即移出错题本(视为已掌握)。
// id 越界或本来就不在本上时什么都不做。
void wb_mark_right(wb_book_t *book, uint8_t id);

uint8_t wb_level(const wb_book_t *book, uint8_t id);
uint16_t wb_total(const wb_book_t *book);

// 收集错题 ID,按错次从高到低排(错得多的先练),最多 max 个,返回实际个数。
uint8_t wb_collect(const wb_book_t *book, uint8_t *out, uint8_t max);

// ---------------------------------------------------------------- 持久化单例

// 载入。须在 nvs_flash_init() 之后调用。
void wb_init(void);

// 记一次作答。reviewing 为 true 表示这题是在错题本模式下答的:
//   * 答错                → 记入错题本
//   * 错题本模式下答对    → 错次递减
//   * 普通模式下答对      → 不改动(反复做对不等于已掌握,留着复习)
void wb_note(uint8_t id, bool right, bool reviewing);

// 当前错题本题数。
uint16_t wb_count(void);

// 取出错题 ID(供 pz_session_start_ids 使用),返回实际个数。
uint8_t wb_ids(uint8_t *out, uint8_t max);
