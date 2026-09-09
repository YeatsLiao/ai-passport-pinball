// main/pb_assets.c —— 由 tools/gen_assets.py 生成,请勿手改。
// 重新生成: python tools/gen_assets.py --preview
#include "pb_assets.h"

extern const uint8_t pb_art_bin_start[] asm("_binary_pb_art_bin_start");

#define PB_ART(off) (pb_art_bin_start + (off))

const lv_image_dsc_t pb_img_bg = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .flags = 0,
        .w = 240,
        .h = 320,
        .stride = 480,
    },
    .data_size = 153600,
    .data = PB_ART(0),
};
const lv_image_dsc_t pb_img_ball = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 16,
        .h = 16,
        .stride = 32,
    },
    .data_size = 768,
    .data = PB_ART(153600),
};
const int16_t pb_ball_ofs[2] = { -8, -8 };

const lv_image_dsc_t pb_img_hole_0 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 30,
        .h = 30,
        .stride = 60,
    },
    .data_size = 2700,
    .data = PB_ART(154368),
};
const lv_image_dsc_t pb_img_hole_1 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 30,
        .h = 30,
        .stride = 60,
    },
    .data_size = 2700,
    .data = PB_ART(157068),
};
const lv_image_dsc_t *const pb_img_hole[2] = { &pb_img_hole_0, &pb_img_hole_1 };
const int16_t pb_hole_pos[2] = { 89, 185 };

const lv_image_dsc_t pb_img_shadow = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 14,
        .h = 10,
        .stride = 28,
    },
    .data_size = 420,
    .data = PB_ART(159768),
};
const int16_t pb_shadow_ofs[2] = { -7, -5 };

const lv_image_dsc_t pb_img_flip_0_0 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 39,
        .h = 30,
        .stride = 78,
    },
    .data_size = 3510,
    .data = PB_ART(160188),
};
const lv_image_dsc_t pb_img_flip_0_1 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 40,
        .h = 27,
        .stride = 80,
    },
    .data_size = 3240,
    .data = PB_ART(163700),
};
const lv_image_dsc_t pb_img_flip_0_2 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 41,
        .h = 25,
        .stride = 82,
    },
    .data_size = 3075,
    .data = PB_ART(166940),
};
const lv_image_dsc_t pb_img_flip_0_3 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 42,
        .h = 22,
        .stride = 84,
    },
    .data_size = 2772,
    .data = PB_ART(170016),
};
const lv_image_dsc_t pb_img_flip_0_4 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 42,
        .h = 19,
        .stride = 84,
    },
    .data_size = 2394,
    .data = PB_ART(172788),
};
const lv_image_dsc_t pb_img_flip_0_5 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 42,
        .h = 17,
        .stride = 84,
    },
    .data_size = 2142,
    .data = PB_ART(175184),
};
const lv_image_dsc_t pb_img_flip_0_6 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 42,
        .h = 15,
        .stride = 84,
    },
    .data_size = 1890,
    .data = PB_ART(177328),
};
const lv_image_dsc_t pb_img_flip_0_7 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 42,
        .h = 15,
        .stride = 84,
    },
    .data_size = 1890,
    .data = PB_ART(179220),
};
const lv_image_dsc_t pb_img_flip_0_8 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 42,
        .h = 18,
        .stride = 84,
    },
    .data_size = 2268,
    .data = PB_ART(181112),
};
const lv_image_dsc_t pb_img_flip_0_9 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 42,
        .h = 21,
        .stride = 84,
    },
    .data_size = 2646,
    .data = PB_ART(183380),
};
const lv_image_dsc_t pb_img_flip_0_10 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 41,
        .h = 24,
        .stride = 82,
    },
    .data_size = 2952,
    .data = PB_ART(186028),
};
const lv_image_dsc_t pb_img_flip_0_11 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 40,
        .h = 27,
        .stride = 80,
    },
    .data_size = 3240,
    .data = PB_ART(188980),
};
const lv_image_dsc_t pb_img_flip_1_0 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 37,
        .h = 30,
        .stride = 74,
    },
    .data_size = 3330,
    .data = PB_ART(192220),
};
const lv_image_dsc_t pb_img_flip_1_1 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 38,
        .h = 27,
        .stride = 76,
    },
    .data_size = 3078,
    .data = PB_ART(195552),
};
const lv_image_dsc_t pb_img_flip_1_2 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 40,
        .h = 25,
        .stride = 80,
    },
    .data_size = 3000,
    .data = PB_ART(198632),
};
const lv_image_dsc_t pb_img_flip_1_3 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 40,
        .h = 22,
        .stride = 80,
    },
    .data_size = 2640,
    .data = PB_ART(201632),
};
const lv_image_dsc_t pb_img_flip_1_4 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 41,
        .h = 19,
        .stride = 82,
    },
    .data_size = 2337,
    .data = PB_ART(204272),
};
const lv_image_dsc_t pb_img_flip_1_5 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 41,
        .h = 17,
        .stride = 82,
    },
    .data_size = 2091,
    .data = PB_ART(206612),
};
const lv_image_dsc_t pb_img_flip_1_6 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 41,
        .h = 15,
        .stride = 82,
    },
    .data_size = 1845,
    .data = PB_ART(208704),
};
const lv_image_dsc_t pb_img_flip_1_7 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 41,
        .h = 15,
        .stride = 82,
    },
    .data_size = 1845,
    .data = PB_ART(210552),
};
const lv_image_dsc_t pb_img_flip_1_8 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 41,
        .h = 18,
        .stride = 82,
    },
    .data_size = 2214,
    .data = PB_ART(212400),
};
const lv_image_dsc_t pb_img_flip_1_9 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 40,
        .h = 21,
        .stride = 80,
    },
    .data_size = 2520,
    .data = PB_ART(214616),
};
const lv_image_dsc_t pb_img_flip_1_10 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 40,
        .h = 24,
        .stride = 80,
    },
    .data_size = 2880,
    .data = PB_ART(217136),
};
const lv_image_dsc_t pb_img_flip_1_11 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 39,
        .h = 27,
        .stride = 78,
    },
    .data_size = 3159,
    .data = PB_ART(220016),
};
const lv_image_dsc_t *const pb_img_flip[2][12] = {
    { &pb_img_flip_0_0, &pb_img_flip_0_1, &pb_img_flip_0_2, &pb_img_flip_0_3, &pb_img_flip_0_4, &pb_img_flip_0_5, &pb_img_flip_0_6, &pb_img_flip_0_7, &pb_img_flip_0_8, &pb_img_flip_0_9, &pb_img_flip_0_10, &pb_img_flip_0_11 },
    { &pb_img_flip_1_0, &pb_img_flip_1_1, &pb_img_flip_1_2, &pb_img_flip_1_3, &pb_img_flip_1_4, &pb_img_flip_1_5, &pb_img_flip_1_6, &pb_img_flip_1_7, &pb_img_flip_1_8, &pb_img_flip_1_9, &pb_img_flip_1_10, &pb_img_flip_1_11 },
};

const int16_t pb_flip_ofs[2][12][2] = {
    { { -5, -6 }, { -5, -6 }, { -5, -6 }, { -5, -6 }, { -5, -6 }, { -5, -7 }, { -5, -7 }, { -5, -7 }, { -5, -10 }, { -5, -13 }, { -5, -16 }, { -5, -19 } },
    { { -32, -6 }, { -33, -6 }, { -35, -6 }, { -35, -6 }, { -36, -6 }, { -36, -7 }, { -36, -7 }, { -36, -7 }, { -36, -10 }, { -35, -13 }, { -35, -16 }, { -34, -19 } },
};

const lv_image_dsc_t pb_img_bump_0_0 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 26,
        .h = 26,
        .stride = 52,
    },
    .data_size = 2028,
    .data = PB_ART(223176),
};
const lv_image_dsc_t pb_img_bump_0_1 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 26,
        .h = 26,
        .stride = 52,
    },
    .data_size = 2028,
    .data = PB_ART(225204),
};
const lv_image_dsc_t pb_img_bump_1_0 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 22,
        .h = 22,
        .stride = 44,
    },
    .data_size = 1452,
    .data = PB_ART(227232),
};
const lv_image_dsc_t pb_img_bump_1_1 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 22,
        .h = 22,
        .stride = 44,
    },
    .data_size = 1452,
    .data = PB_ART(228684),
};
const lv_image_dsc_t pb_img_bump_2_0 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 22,
        .h = 22,
        .stride = 44,
    },
    .data_size = 1452,
    .data = PB_ART(230136),
};
const lv_image_dsc_t pb_img_bump_2_1 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 22,
        .h = 22,
        .stride = 44,
    },
    .data_size = 1452,
    .data = PB_ART(231588),
};
const lv_image_dsc_t pb_img_bump_3_0 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 14,
        .h = 14,
        .stride = 28,
    },
    .data_size = 588,
    .data = PB_ART(233040),
};
const lv_image_dsc_t pb_img_bump_3_1 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 14,
        .h = 14,
        .stride = 28,
    },
    .data_size = 588,
    .data = PB_ART(233628),
};
const lv_image_dsc_t pb_img_bump_4_0 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 14,
        .h = 14,
        .stride = 28,
    },
    .data_size = 588,
    .data = PB_ART(234216),
};
const lv_image_dsc_t pb_img_bump_4_1 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 14,
        .h = 14,
        .stride = 28,
    },
    .data_size = 588,
    .data = PB_ART(234804),
};
const lv_image_dsc_t pb_img_bump_5_0 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 12,
        .h = 12,
        .stride = 24,
    },
    .data_size = 432,
    .data = PB_ART(235392),
};
const lv_image_dsc_t pb_img_bump_5_1 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 12,
        .h = 12,
        .stride = 24,
    },
    .data_size = 432,
    .data = PB_ART(235824),
};
const lv_image_dsc_t pb_img_bump_6_0 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 12,
        .h = 12,
        .stride = 24,
    },
    .data_size = 432,
    .data = PB_ART(236256),
};
const lv_image_dsc_t pb_img_bump_6_1 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 12,
        .h = 12,
        .stride = 24,
    },
    .data_size = 432,
    .data = PB_ART(236688),
};
const lv_image_dsc_t pb_img_bump_7_0 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 12,
        .h = 12,
        .stride = 24,
    },
    .data_size = 432,
    .data = PB_ART(237120),
};
const lv_image_dsc_t pb_img_bump_7_1 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 12,
        .h = 12,
        .stride = 24,
    },
    .data_size = 432,
    .data = PB_ART(237552),
};
const lv_image_dsc_t pb_img_bump_8_0 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 12,
        .h = 12,
        .stride = 24,
    },
    .data_size = 432,
    .data = PB_ART(237984),
};
const lv_image_dsc_t pb_img_bump_8_1 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 12,
        .h = 12,
        .stride = 24,
    },
    .data_size = 432,
    .data = PB_ART(238416),
};
const lv_image_dsc_t pb_img_bump_9_0 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 12,
        .h = 12,
        .stride = 24,
    },
    .data_size = 432,
    .data = PB_ART(238848),
};
const lv_image_dsc_t pb_img_bump_9_1 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 12,
        .h = 12,
        .stride = 24,
    },
    .data_size = 432,
    .data = PB_ART(239280),
};
const lv_image_dsc_t *const pb_img_bump[10][2] = {
    { &pb_img_bump_0_0, &pb_img_bump_0_1 },
    { &pb_img_bump_1_0, &pb_img_bump_1_1 },
    { &pb_img_bump_2_0, &pb_img_bump_2_1 },
    { &pb_img_bump_3_0, &pb_img_bump_3_1 },
    { &pb_img_bump_4_0, &pb_img_bump_4_1 },
    { &pb_img_bump_5_0, &pb_img_bump_5_1 },
    { &pb_img_bump_6_0, &pb_img_bump_6_1 },
    { &pb_img_bump_7_0, &pb_img_bump_7_1 },
    { &pb_img_bump_8_0, &pb_img_bump_8_1 },
    { &pb_img_bump_9_0, &pb_img_bump_9_1 },
};

const int16_t pb_bump_pos[10][2] = {
    { 94, 71 },
    { 59, 99 },
    { 133, 99 },
    { 51, 173 },
    { 149, 173 },
    { 72, 202 },
    { 130, 202 },
    { 183, 144 },
    { 183, 161 },
    { 183, 178 },
};

const lv_image_dsc_t pb_img_lane_0 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 18,
        .h = 14,
        .stride = 36,
    },
    .data_size = 756,
    .data = PB_ART(239712),
};
const lv_image_dsc_t pb_img_lane_1 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 18,
        .h = 14,
        .stride = 36,
    },
    .data_size = 756,
    .data = PB_ART(240468),
};
const lv_image_dsc_t *const pb_img_lane[2] = { &pb_img_lane_0, &pb_img_lane_1 };
const int16_t pb_lane_pos[3][2] = {
    { 58, 35 },
    { 98, 35 },
    { 138, 35 },
};

const lv_image_dsc_t pb_img_tgt_0 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 24,
        .h = 18,
        .stride = 48,
    },
    .data_size = 1296,
    .data = PB_ART(241224),
};
const lv_image_dsc_t pb_img_tgt_1 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 24,
        .h = 18,
        .stride = 48,
    },
    .data_size = 1296,
    .data = PB_ART(242520),
};
const lv_image_dsc_t *const pb_img_tgt[2] = { &pb_img_tgt_0, &pb_img_tgt_1 };
const int16_t pb_tgt_pos[3][2] = {
    { 21, 121 },
    { 21, 143 },
    { 21, 165 },
};

const lv_image_dsc_t pb_img_sling_0_0 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 34,
        .h = 30,
        .stride = 68,
    },
    .data_size = 3060,
    .data = PB_ART(243816),
};
const lv_image_dsc_t pb_img_sling_0_1 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 34,
        .h = 30,
        .stride = 68,
    },
    .data_size = 3060,
    .data = PB_ART(246876),
};
const lv_image_dsc_t pb_img_sling_1_0 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 34,
        .h = 30,
        .stride = 68,
    },
    .data_size = 3060,
    .data = PB_ART(249936),
};
const lv_image_dsc_t pb_img_sling_1_1 = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565A8,
        .flags = 0,
        .w = 34,
        .h = 30,
        .stride = 68,
    },
    .data_size = 3060,
    .data = PB_ART(252996),
};
const lv_image_dsc_t *const pb_img_sling[2][2] = {
    { &pb_img_sling_0_0, &pb_img_sling_0_1 },
    { &pb_img_sling_1_0, &pb_img_sling_1_1 },
};
const int16_t pb_sling_pos[2][2] = {
    { 41, 232 },
    { 139, 232 },
};
