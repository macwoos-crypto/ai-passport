// tests/test_pinyin_quiz.c —— 拼音易错题:题库完整性 + 答题状态机。
//
// 题库最容易出的错不是逻辑,而是内容:正确答案下标写越界、两个选项写重复、
// 某个题型区间重叠。这些都靠下面的断言挡住。
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pinyin_quiz.h"

static void test_bank_layout(void) {
    assert(PZ_SET_COUNT == 7);
    // 下标 0 是"全部混合",池大小必须等于题库总数。
    assert(PZ_SETS[0].offset == 0);
    assert(PZ_SETS[0].count == PZ_ITEM_COUNT);

    // 各分类必须首尾相接、完整覆盖题库,不重不漏。
    unsigned sum = 0;
    for (uint8_t i = 1; i < PZ_SET_COUNT; i++) {
        assert(PZ_SETS[i].count > 0);
        assert(PZ_SETS[i].offset == sum);
        sum += PZ_SETS[i].count;
    }
    assert(sum == PZ_ITEM_COUNT);
    // 混一局不能比一道题还短,也不能超过缓冲上限。
    assert(PZ_MIXED_LIMIT > 1);
    assert(PZ_MIXED_LIMIT <= PZ_MAX_QUESTIONS);
}

static void test_bank_items(void) {
    for (uint16_t i = 0; i < PZ_ITEM_COUNT; i++) {
        const pz_item_t *it = &PZ_ITEMS[i];
        assert(it->word != NULL && it->word[0] != '\0');
        assert(it->hint != NULL && it->hint[0] != '\0');
        assert(it->correct < PZ_OPTION_COUNT);

        for (uint8_t a = 0; a < PZ_OPTION_COUNT; a++) {
            assert(it->options[a] != NULL && it->options[a][0] != '\0');
            for (uint8_t b = (uint8_t)(a + 1); b < PZ_OPTION_COUNT; b++) {
                // 重复选项会让一道题出现两个"正确"答案。
                assert(strcmp(it->options[a], it->options[b]) != 0);
            }
        }
    }
}

static void test_start_and_phases(void) {
    pz_session_t s;
    pz_session_start(&s, 1, 1234);
    assert(s.phase == PZ_PHASE_ASK);
    assert(s.total == PZ_SETS[1].count);
    assert(s.index == 0);
    assert(s.score == 0);
    assert(s.cursor == 0);
    assert(s.answer_slot == 0xFF);

    // 光标上下回绕,不会越界。
    pz_session_move(&s, -1);
    assert(s.cursor == PZ_OPTION_COUNT - 1);
    pz_session_move(&s, 1);
    assert(s.cursor == 0);
    for (uint8_t i = 0; i < PZ_OPTION_COUNT; i++) pz_session_move(&s, 1);
    assert(s.cursor == 0);
}

static void test_confirm_scoring(void) {
    pz_session_t s;
    pz_session_start(&s, 2, 77);

    // 答对:光标落到正确位置,分数加一,进入解析阶段。
    s.cursor = s.correct_slot;
    assert(pz_session_confirm(&s) == true);
    assert(s.score == 1);
    assert(s.phase == PZ_PHASE_REVEAL);
    assert(s.last_right == true);
    // REVEAL 阶段重复确认不应该再加分。
    assert(pz_session_confirm(&s) == false);
    assert(s.score == 1);
    // REVEAL 阶段移动光标无效。
    uint8_t before = s.cursor;
    pz_session_move(&s, 1);
    assert(s.cursor == before);

    assert(pz_session_advance(&s) == true);
    assert(s.phase == PZ_PHASE_ASK);
    assert(s.index == 1);

    // 答错:分数不变,但仍进入解析阶段。
    s.cursor = (uint8_t)((s.correct_slot + 1) % PZ_OPTION_COUNT);
    assert(pz_session_confirm(&s) == false);
    assert(s.score == 1);
    assert(s.last_right == false);
}

static void test_full_round(void) {
    pz_session_t s;
    pz_session_start(&s, 3, 9);
    const uint8_t total = s.total;

    for (uint8_t i = 0; i < total; i++) {
        assert(s.phase == PZ_PHASE_ASK);
        assert(s.index == i);
        s.cursor = s.correct_slot;
        assert(pz_session_confirm(&s) == true);
        (void)pz_session_advance(&s);
    }

    assert(s.phase == PZ_PHASE_DONE);
    assert(s.score == total);
    assert(pz_percent(&s) == 100);
    // 结束阶段再推进不会再出题。
    assert(pz_session_advance(&s) == false);
    assert(s.phase == PZ_PHASE_DONE);
    // 全部答对时"当前题"仍可安全取到,方便结算页画封面。
    assert(pz_current_item(&s) != NULL);
}

static void test_wrong_answers_percent(void) {
    pz_session_t s;
    pz_session_start(&s, 4, 21);
    const uint8_t total = s.total;

    for (uint8_t i = 0; i < total; i++) {
        // 前一半故意选错,后一半选对。
        s.cursor = (i < total / 2) ? (uint8_t)((s.correct_slot + 1) % PZ_OPTION_COUNT)
                                   : s.correct_slot;
        (void)pz_session_confirm(&s);
        (void)pz_session_advance(&s);
    }

    assert(s.phase == PZ_PHASE_DONE);
    assert(s.score == (uint8_t)(total - total / 2));
    uint8_t expected = (uint8_t)(((uint16_t)s.score * 100u) / total);
    assert(pz_percent(&s) == expected);
    assert(pz_verdict(pz_percent(&s)) != NULL);
}

static void test_slot_mapping(void) {
    pz_session_t s;
    pz_session_start(&s, 5, 4321);
    const pz_item_t *it = pz_current_item(&s);

    // 显示位置的文本必须真的来自题库,且正确位置指的就是正确答案。
    for (uint8_t slot = 0; slot < PZ_OPTION_COUNT; slot++) {
        const char *text = pz_slot_text(&s, slot);
        bool found = false;
        for (uint8_t i = 0; i < PZ_OPTION_COUNT; i++) {
            if (strcmp(text, it->options[i]) == 0) found = true;
        }
        assert(found);
    }
    assert(strcmp(pz_slot_text(&s, s.correct_slot), it->options[it->correct]) == 0);
    assert(pz_slot_text(&s, PZ_OPTION_COUNT) != NULL);
    assert(pz_slot_text(&s, PZ_OPTION_COUNT)[0] == '\0');
}

static void test_mixed_limit(void) {
    pz_session_t s;
    pz_session_start(&s, 0, 5);
    assert(s.total == PZ_MIXED_LIMIT);
    assert(s.total < PZ_ITEM_COUNT);

    // 混合练习抽到的题必须落在整个题库范围内,且不重复。
    bool seen[PZ_MAX_QUESTIONS] = { 0 };
    for (uint8_t i = 0; i < s.total; i++) {
        uint8_t id = s.order[i];
        assert(id < PZ_ITEM_COUNT);
        assert(!seen[id]);
        seen[id] = true;
    }
}

static void test_deterministic_order(void) {
    pz_session_t a;
    pz_session_t b;
    pz_session_start(&a, 6, 2026);
    pz_session_start(&b, 6, 2026);
    assert(memcmp(a.order, b.order, sizeof(a.order)) == 0);
    assert(memcmp(a.perm, b.perm, sizeof(a.perm)) == 0);

    // seed 为 0 会被夹到 1,不能退化成一局没有随机的题。
    pz_session_t c;
    pz_session_start(&c, 6, 0);
    assert(c.rng != 0);

    // 不同 seed 至少有一次顺序不同(48 题全部同序的概率极低)。
    pz_session_t d;
    pz_session_start(&d, 6, 2027);
    assert(memcmp(a.order, d.order, sizeof(a.order)) != 0
           || memcmp(a.perm, d.perm, sizeof(a.perm)) != 0);
}

static void test_bad_set_index(void) {
    pz_session_t s;
    // 越界的题型下标应该退到"全部混合",而不是读越界内存。
    pz_session_start(&s, 200, 3);
    assert(s.phase == PZ_PHASE_ASK);
    assert(s.offset == PZ_SETS[0].offset);
    assert(s.total == PZ_MIXED_LIMIT);
}

// 题目 ID 必须和当前题指针一致:错题本按 ID 记账,这里错了会记到别的题上。
static void test_current_id_matches_item(void) {
    pz_session_t s;
    pz_session_start(&s, 2, 55);
    for (uint8_t i = 0; i < s.total; i++) {
        assert(s.phase == PZ_PHASE_ASK);
        uint8_t id = pz_current_id(&s);
        assert(id < PZ_BANK_SIZE);
        assert(&PZ_ITEMS[id] == pz_current_item(&s));
        s.cursor = s.correct_slot;
        assert(pz_session_confirm(&s));
        (void)pz_session_advance(&s);
    }
    assert(s.phase == PZ_PHASE_DONE);
}

// 混合练习必须从整个题库里抽,而不是永远取前 PZ_MIXED_LIMIT 道。
// (曾经把"按 pool 打乱"改成"按 total 打乱",就退化成只练前 20 道了。)
static void test_mixed_samples_whole_bank(void) {
    bool saw_high = false;
    for (uint16_t seed = 1; seed <= 20 && !saw_high; seed++) {
        pz_session_t s;
        pz_session_start(&s, 0, seed);
        assert(s.total == PZ_MIXED_LIMIT);
        for (uint8_t i = 0; i < s.total; i++) {
            if (s.order[i] >= PZ_MIXED_LIMIT) {
                saw_high = true;
                break;
            }
        }
    }
    assert(saw_high);
}

static void test_start_ids(void) {
    const uint8_t ids[] = { 3, 7, 11, 40 };
    pz_session_t s;
    pz_session_start_ids(&s, ids, 4, 123);

    assert(s.total == 4);
    assert(s.offset == 0);
    assert(s.phase == PZ_PHASE_ASK);

    bool seen[PZ_BANK_SIZE] = { 0 };
    for (uint8_t i = 0; i < s.total; i++) {
        uint8_t id = pz_current_id(&s);
        assert(id == 3 || id == 7 || id == 11 || id == 40);
        // 四道题各出现一次,不能重复。
        assert(!seen[id]);
        seen[id] = true;
        assert(&PZ_ITEMS[id] == pz_current_item(&s));

        s.cursor = s.correct_slot;
        assert(pz_session_confirm(&s));
        (void)pz_session_advance(&s);
    }
    assert(s.phase == PZ_PHASE_DONE);
    assert(s.score == 4);
}

static void test_start_ids_edges(void) {
    pz_session_t s;

    // 空错题本:直接结束,取当前题也必须安全(不能把下标回绕成 255)。
    pz_session_start_ids(&s, NULL, 0, 1);
    assert(s.total == 0);
    assert(s.phase == PZ_PHASE_DONE);
    assert(pz_current_item(&s) != NULL);
    assert(pz_current_id(&s) < PZ_BANK_SIZE);
    assert(pz_percent(&s) == 0);

    // 越界 ID 被夹到 0,不拿脏下标去索引题库。
    const uint8_t bad[] = { 200, 3, 255 };
    pz_session_start_ids(&s, bad, 3, 7);
    assert(s.total == 3);
    for (uint8_t i = 0; i < s.total; i++) {
        uint8_t id = pz_current_id(&s);
        assert(id < PZ_BANK_SIZE);
        s.cursor = s.correct_slot;
        (void)pz_session_confirm(&s);
        (void)pz_session_advance(&s);
    }

    // count 超过题库上限时被截断,不能写到 order[] 之外。
    uint8_t many[128];
    for (uint8_t i = 0; i < (uint8_t)sizeof(many); i++) many[i] = (uint8_t)(i % PZ_BANK_SIZE);
    pz_session_start_ids(&s, many, 200, 9);
    assert(s.total <= PZ_MAX_QUESTIONS);
    assert(s.total <= PZ_BANK_SIZE);
}

int main(void) {
    test_bank_layout();
    test_bank_items();
    test_start_and_phases();
    test_confirm_scoring();
    test_full_round();
    test_wrong_answers_percent();
    test_slot_mapping();
    test_mixed_limit();
    test_deterministic_order();
    test_bad_set_index();
    test_current_id_matches_item();
    test_mixed_samples_whole_bank();
    test_start_ids();
    test_start_ids_edges();
    printf("test_pinyin_quiz: PASS (%u questions)\n", (unsigned)PZ_ITEM_COUNT);
    return 0;
}
