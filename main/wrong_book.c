// main/wrong_book.c —— 错题本的 NVS 持久化层。
//
// 存储格式(blob):'P' 'Y' <ver> <counts...>
// 用定长 blob 而不是逐条 NVS key:48 道题的错次一次读写完成,掉电只会整体回到
// 上一次写入点,不会出现"记了一半"的中间状态。
#include "wrong_book.h"

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "wrongbook";

#define WB_NS      "pinyin"
#define WB_KEY     "wrong"
#define WB_MAGIC0  'P'
#define WB_MAGIC1  'Y'
#define WB_VER     1
// 头部 3 字节:两个魔数 + 版本。改动题目数量时把 WB_VER 加一,旧数据会自动作废。
#define WB_HEAD    (3 + PZ_BANK_SIZE)

static wb_book_t s_book;
static bool s_muted;   // NVS 打不开时只抱怨一次,不要每次答题都刷日志

static void wb_store(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WB_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        if (!s_muted) {
            ESP_LOGW(TAG, "NVS 打不开(%s),本次错题不会保存", esp_err_to_name(err));
            s_muted = true;
        }
        return;
    }

    uint8_t blob[WB_HEAD];
    blob[0] = WB_MAGIC0;
    blob[1] = WB_MAGIC1;
    blob[2] = WB_VER;
    for (uint8_t i = 0; i < PZ_BANK_SIZE; i++) blob[3 + i] = s_book.counts[i];

    err = nvs_set_blob(handle, WB_KEY, blob, sizeof(blob));
    if (err == ESP_OK) err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "错题本写入失败: %s", esp_err_to_name(err));
    }
    nvs_close(handle);
}

void wb_init(void) {
    wb_reset(&s_book);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(WB_NS, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        // 第一次开机不会有这个命名空间,这是正常路径,不是错误。
        ESP_LOGI(TAG, "错题本为空(首次使用)");
        return;
    }

    uint8_t blob[WB_HEAD];
    size_t len = sizeof(blob);
    err = nvs_get_blob(handle, WB_KEY, blob, &len);
    nvs_close(handle);

    const bool valid = err == ESP_OK && len == WB_HEAD
                    && blob[0] == WB_MAGIC0 && blob[1] == WB_MAGIC1
                    && blob[2] == WB_VER;
    if (!valid) {
        // 版本不符或内容损坏:当空本处理,不拿半截数据去出题。
        ESP_LOGI(TAG, "错题本无有效数据,按空本启动");
        return;
    }

    for (uint8_t i = 0; i < PZ_BANK_SIZE; i++) s_book.counts[i] = blob[3 + i];
    ESP_LOGI(TAG, "错题本载入:%d 题待复习", (int)wb_total(&s_book));
}

void wb_note(uint8_t id, bool right, bool reviewing) {
    if (id >= PZ_BANK_SIZE) return;
    const uint8_t before = s_book.counts[id];

    if (!right) {
        wb_mark_wrong(&s_book, id);
    } else if (reviewing) {
        wb_mark_right(&s_book, id);
    } else {
        // 普通练习里做对不改动错题本:做对一次不等于已掌握,留着继续复习。
        return;
    }

    if (s_book.counts[id] != before) wb_store();
}

uint16_t wb_count(void) {
    return wb_total(&s_book);
}

uint8_t wb_ids(uint8_t *out, uint8_t max) {
    return wb_collect(&s_book, out, max);
}
