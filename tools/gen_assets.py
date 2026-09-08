#!/usr/bin/env python3
# tools/gen_assets.py —— 生成台面美术资产并打包进固件。
#
# 产物:
#   main/assets/pb_art.bin   背景位图 + 全部精灵,4 字节对齐顺序打包
#   main/pb_assets.c/.h      lv_image_dsc_t 资产表(指向 bin 内的偏移)+ 各精灵锚点坐标
#   build/art/*.png          --preview 时导出的预览图(供肉眼校对美术)
#
# 为什么走 EMBED_FILES 而不是 C 数组:背景一张就 150KB,写成 C 字面量是近 1MB 的
# 源文件,每次改美术都要重编一遍;嵌二进制进 .rodata 编译期零成本,且从 flash
# 直接 XIP 读,不占 RAM(C3 没有 PSRAM,这点是硬约束)。
#
# 用法:
#   python tools/gen_assets.py --preview
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from PIL import Image  # noqa: E402

import pb_art as A  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN_PATH = os.path.join(ROOT, "main", "assets", "pb_art.bin")
OUT_C = os.path.join(ROOT, "main", "pb_assets.c")
OUT_H = os.path.join(ROOT, "main", "pb_assets.h")
PREVIEW_DIR = os.path.join(ROOT, "build", "art")

BIN_SYMBOL = "_binary_pb_art_bin_start"


def align4(n):
    return (n + 3) & ~3


class Blob:
    def __init__(self):
        self.data = bytearray()
        self.items = []          # (name, cf, w, h, stride, offset, size)

    def add(self, name, cf, w, h, stride, payload):
        off = len(self.data)
        assert off % 4 == 0, "blob 偏移必须 4 字节对齐"
        self.data += payload
        # RGB565 用 uint16 读写,落在奇数地址会在 C3 上触发非对齐访问,补齐到 4
        pad = align4(len(self.data)) - len(self.data)
        self.data += b"\0" * pad
        self.items.append(dict(name=name, cf=cf, w=w, h=h, stride=stride,
                               off=off, size=len(payload)))
        return off, len(payload)


def build_blob(g, bg_img, sp):
    blob = Blob()
    blob.add("pb_img_bg", "LV_COLOR_FORMAT_RGB565", A.W, A.H, A.W * 2,
             A.image_to_rgb565(bg_img))

    def add_sp(name, s):
        blob.add(name, "LV_COLOR_FORMAT_RGB565A8", s["w"], s["h"], s["w"] * 2,
                 A.sprite_to_rgb565a8(s))

    add_sp("pb_img_ball", sp["ball"][0])
    for side, frames in enumerate(sp["flip"]):
        for i, s in enumerate(frames):
            add_sp("pb_img_flip_%d_%d" % (side, i), s)
    for i, pair in enumerate(sp["bump"]):
        for j, s in enumerate(pair):
            add_sp("pb_img_bump_%d_%d" % (i, j), s)
    for j, s in enumerate(sp["lane"]):
        add_sp("pb_img_lane_%d" % j, s)
    for j, s in enumerate(sp["tgt"]):
        add_sp("pb_img_tgt_%d" % j, s)
    for i, pair in enumerate(sp["sling"]):
        for j, s in enumerate(pair):
            add_sp("pb_img_sling_%d_%d" % (i, j), s)
    return blob


def emit_c(blob, g, sp, meta):
    nf = meta["flip_frames"]
    nb = len(sp["bump"])
    nl = len(g["lanes"])
    nt = len(g["targets"])
    ns = len(sp["sling"])

    def dsc(name, it):
        return (
            "const lv_image_dsc_t %s = {\n"
            "    .header = {\n"
            "        .magic = LV_IMAGE_HEADER_MAGIC,\n"
            "        .cf = %s,\n"
            "        .flags = 0,\n"
            "        .w = %d,\n"
            "        .h = %d,\n"
            "        .stride = %d,\n"
            "    },\n"
            "    .data_size = %d,\n"
            "    .data = PB_ART(%d),\n"
            "};\n" % (name, it["cf"], it["w"], it["h"], it["stride"],
                      it["size"], it["off"]))

    by_name = {it["name"]: it for it in blob.items}
    L = []
    L.append("// main/pb_assets.c —— 由 tools/gen_assets.py 生成,请勿手改。\n"
             "// 重新生成: python tools/gen_assets.py --preview\n"
             '#include "pb_assets.h"\n\n'
             "extern const uint8_t pb_art_bin_start[] asm(\"%s\");\n\n"
             "#define PB_ART(off) (pb_art_bin_start + (off))\n\n" % BIN_SYMBOL)

    L.append(dsc("pb_img_bg", by_name["pb_img_bg"]))
    L.append(dsc("pb_img_ball", by_name["pb_img_ball"]))
    L.append("const int16_t pb_ball_ofs[2] = { %d, %d };\n\n"
             % (sp["ball"][0]["ox"], sp["ball"][0]["oy"]))

    # 挡板:每帧包围盒不同,必须把"包围盒左上角相对转轴"的偏移一起导出,
    # 渲染层才能把精灵贴回正确的枢轴位置。
    for side in range(2):
        for i in range(nf):
            L.append(dsc("pb_img_flip_%d_%d" % (side, i),
                         by_name["pb_img_flip_%d_%d" % (side, i)]))
    L.append("const lv_image_dsc_t *const pb_img_flip[2][%d] = {\n" % nf)
    for side in range(2):
        L.append("    { " + ", ".join("&pb_img_flip_%d_%d" % (side, i)
                                      for i in range(nf)) + " },\n")
    L.append("};\n\n")
    L.append("const int16_t pb_flip_ofs[2][%d][2] = {\n" % nf)
    for side in range(2):
        L.append("    { " + ", ".join(
            "{ %d, %d }" % (sp["flip"][side][i]["ox"], sp["flip"][side][i]["oy"])
            for i in range(nf)) + " },\n")
    L.append("};\n\n")

    for i in range(nb):
        for j in range(2):
            L.append(dsc("pb_img_bump_%d_%d" % (i, j),
                         by_name["pb_img_bump_%d_%d" % (i, j)]))
    L.append("const lv_image_dsc_t *const pb_img_bump[%d][2] = {\n" % nb)
    for i in range(nb):
        L.append("    { &pb_img_bump_%d_0, &pb_img_bump_%d_1 },\n" % (i, i))
    L.append("};\n\n")
    L.append("const int16_t pb_bump_pos[%d][2] = {\n" % nb)
    for i in range(nb):
        L.append("    { %d, %d },\n" % (sp["bump"][i][0]["ox"], sp["bump"][i][0]["oy"]))
    L.append("};\n\n")

    for j in range(2):
        L.append(dsc("pb_img_lane_%d" % j, by_name["pb_img_lane_%d" % j]))
    L.append("const lv_image_dsc_t *const pb_img_lane[2] = "
             "{ &pb_img_lane_0, &pb_img_lane_1 };\n")
    L.append("const int16_t pb_lane_pos[%d][2] = {\n" % nl)
    for p in meta["lane_pos"]:
        L.append("    { %d, %d },\n" % (int(round(p[0])), int(round(p[1]))))
    L.append("};\n\n")

    for j in range(2):
        L.append(dsc("pb_img_tgt_%d" % j, by_name["pb_img_tgt_%d" % j]))
    L.append("const lv_image_dsc_t *const pb_img_tgt[2] = "
             "{ &pb_img_tgt_0, &pb_img_tgt_1 };\n")
    L.append("const int16_t pb_tgt_pos[%d][2] = {\n" % nt)
    for p in meta["tgt_pos"]:
        L.append("    { %d, %d },\n" % (int(round(p[0])), int(round(p[1]))))
    L.append("};\n\n")

    for i in range(ns):
        for j in range(2):
            L.append(dsc("pb_img_sling_%d_%d" % (i, j),
                         by_name["pb_img_sling_%d_%d" % (i, j)]))
    L.append("const lv_image_dsc_t *const pb_img_sling[%d][2] = {\n" % ns)
    for i in range(ns):
        L.append("    { &pb_img_sling_%d_0, &pb_img_sling_%d_1 },\n" % (i, i))
    L.append("};\n")
    L.append("const int16_t pb_sling_pos[%d][2] = {\n" % ns)
    for i in range(ns):
        L.append("    { %d, %d },\n" % (sp["sling"][i][0]["ox"], sp["sling"][i][0]["oy"]))
    L.append("};\n")

    open(OUT_C, "w", encoding="utf-8", newline="\n").write("".join(L))

    h = """// main/pb_assets.h —— 由 tools/gen_assets.py 生成,请勿手改。
//
// 台面美术在构建前烘焙成一张 240x320 RGB565 背景 + 一组 RGB565A8 精灵,
// 打包进 main/assets/pb_art.bin(EMBED_FILES 嵌入 .rodata,不占 RAM)。
// 运行时渲染层只做 lv_image 换 src / 挪位置,静态台面一帧都不用重画。
#pragma once

#include <stdint.h>
#include "lvgl.h"

#define PB_ART_FLIP_FRAMES %d
#define PB_ART_BUMP_COUNT  %d
#define PB_ART_LANE_COUNT  %d
#define PB_ART_TGT_COUNT   %d
#define PB_ART_SLING_COUNT %d

// 台面背景(整屏)。
extern const lv_image_dsc_t pb_img_bg;

// 球。pb_ball_ofs 是精灵左上角相对球心的偏移。
extern const lv_image_dsc_t pb_img_ball;
extern const int16_t pb_ball_ofs[2];

// 挡板:[side][frame],frame 0 = 静止,末帧 = 抬起。
// pb_flip_ofs 是各帧包围盒左上角相对转轴的偏移。
extern const lv_image_dsc_t *const pb_img_flip[2][PB_ART_FLIP_FRAMES];
extern const int16_t pb_flip_ofs[2][PB_ART_FLIP_FRAMES][2];

// pop bumper 帽:[i][0]=常态 [i][1]=命中闪光。pb_bump_pos 为屏幕绝对坐标。
extern const lv_image_dsc_t *const pb_img_bump[PB_ART_BUMP_COUNT][2];
extern const int16_t pb_bump_pos[PB_ART_BUMP_COUNT][2];

// 顶部车道灯芯:[0]=灭 [1]=亮,三个车道共用一套。
extern const lv_image_dsc_t *const pb_img_lane[2];
extern const int16_t pb_lane_pos[PB_ART_LANE_COUNT][2];

// 掉落目标:[0]=立着 [1]=放倒,三个目标共用一套。
extern const lv_image_dsc_t *const pb_img_tgt[2];
extern const int16_t pb_tgt_pos[PB_ART_TGT_COUNT][2];

// 弹弓橡皮筋:[side][0]=常态 [1]=闪光。
extern const lv_image_dsc_t *const pb_img_sling[PB_ART_SLING_COUNT][2];
extern const int16_t pb_sling_pos[PB_ART_SLING_COUNT][2];
""" % (nf, nb, nl, nt, ns)
    open(OUT_H, "w", encoding="utf-8", newline="\n").write(h)


# ---------------------------------------------------------------------------
# 预览:把背景 + 精灵按运行时同样的位置合成一张图,肉眼校对美术
# ---------------------------------------------------------------------------

def compose_preview(bg_img, sp, g, meta):
    img = bg_img.convert("RGBA")
    nf = meta["flip_frames"]

    def paste(s, x, y):
        rgba = Image.merge("RGBA", (*s["rgb"].convert("RGB").split(), s["a"]))
        img.alpha_composite(rgba, (int(round(x)), int(round(y))))

    paste(sp["ball"][0], int(96 + sp["ball"][0]["ox"]), int(160 + sp["ball"][0]["oy"]))
    for side, f in enumerate(sorted(g["flippers"], key=lambda d: d["idx"])):
        s = sp["flip"][side][0]
        paste(s, int(f["pivot"][0]) + s["ox"], int(f["pivot"][1]) + s["oy"])
    for i, c in enumerate(g["circles"]):
        paste(sp["bump"][i][1 if i == 0 else 0], sp["bump"][i][0]["ox"],
              sp["bump"][i][0]["oy"])
    for i in range(len(g["lanes"])):
        paste(sp["lane"][1 if i == 1 else 0], *meta["lane_pos"][i])
    for i in range(len(g["targets"])):
        paste(sp["tgt"][0 if i != 2 else 1], *meta["tgt_pos"][i])
    for i in range(len(sp["sling"])):
        paste(sp["sling"][i][1 if i == 0 else 0], sp["sling"][i][0]["ox"],
              sp["sling"][i][0]["oy"])
    return img.convert("RGB")


def save_previews(bg_img, sp, g, meta):
    os.makedirs(PREVIEW_DIR, exist_ok=True)
    shot = compose_preview(bg_img, sp, g, meta)
    bg_img.save(os.path.join(PREVIEW_DIR, "table_bg.png"))
    shot.save(os.path.join(PREVIEW_DIR, "table_shot.png"))
    shot.resize((A.W * 3, A.H * 3), Image.NEAREST).save(
        os.path.join(PREVIEW_DIR, "table_shot_x3.png"))

    # 精灵表:一眼看清每个可换帧部件长什么样
    cells = [("ball", [sp["ball"][0]])]
    cells += [("flip_l", sp["flip"][0]), ("flip_r", sp["flip"][1])]
    cells += [("bump%d" % i, p) for i, p in enumerate(sp["bump"])]
    cells += [("lane", sp["lane"]), ("target", sp["tgt"])]
    cells += [("sling%d" % i, p) for i, p in enumerate(sp["sling"])]
    cw, ch, k = 56, 44, 3
    rows = max(len(lst) for _n, lst in cells)
    sheet = Image.new("RGBA", (cw * len(cells), ch * rows), (16, 22, 34, 255))
    for ci, (_name, lst) in enumerate(cells):
        for ri, s in enumerate(lst):
            rgba = Image.merge("RGBA", (*s["rgb"].convert("RGB").split(), s["a"]))
            sheet.alpha_composite(
                rgba, (ci * cw + (cw - s["w"]) // 2, ri * ch + (ch - s["h"]) // 2))
    sheet.convert("RGB").resize((sheet.width * 2, sheet.height * 2),
                                Image.NEAREST).save(
        os.path.join(PREVIEW_DIR, "sprites.png"))
    print("预览图已导出到 %s" % PREVIEW_DIR)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", action="store_true", help="额外导出 build/art/*.png")
    args = ap.parse_args()

    g = A.parse_geometry(os.path.join(ROOT, "main", "pb_table.c"))
    print("解析几何: %d 线段, %d bumper, %d 挡板, %d 车道"
          % (len(g["segs"]), len(g["circles"]), len(g["flippers"]), len(g["lanes"])))

    bg = A.render_background(g)
    sp, meta = A.render_sprites(g)

    blob = build_blob(g, bg, sp)
    os.makedirs(os.path.dirname(BIN_PATH), exist_ok=True)
    open(BIN_PATH, "wb").write(bytes(blob.data))
    emit_c(blob, g, sp, meta)

    total = len(blob.data)
    print("pb_art.bin: %.1f KB (%d 个资产)" % (total / 1024.0, len(blob.items)))
    for it in blob.items[:3]:
        print("  %-18s %dx%d %s @%d" % (it["name"], it["w"], it["h"], it["cf"], it["off"]))
    print("  ...")
    print("写入 %s" % OUT_C)
    print("写入 %s" % OUT_H)
    if args.preview:
        save_previews(bg, sp, g, meta)


if __name__ == "__main__":
    main()
