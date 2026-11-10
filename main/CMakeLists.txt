idf_component_register(
    SRCS "main.c"
         "app_pinyin.c"
         "pinyin_quiz.c"
         "pinyin_quiz_data.c"
         "wrong_book.c"
         "wrong_book_logic.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_event esp_hw_support esp_netif esp_timer esp_wifi nvs_flash
)

# 拼音调号(ā á ǎ à ǖ ǘ ǚ ǜ)和汉字都不在 Montserrat 里,所以界面字体是从
# assets/fonts/NotoSansSC-Regular.otf 抽出的子集,由 tools/gen_app_fonts.py 生成。
# 改过界面文字或题库之后要重新生成,否则会显示缺字方块。
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/pz_font_16.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/pz_font_22.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/pz_font_32.c"
)
