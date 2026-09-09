#!/usr/bin/env python3
# tools/pb_art.py —— 台面美术库(被 tools/gen_assets.py 调用)。
#
# 设计要点:
# 1. 几何来自 main/pb_table.c(物理是唯一真相源),美术严格贴着碰撞体画,
#    所以改台面坐标后重跑生成器,画的位置和撞的位置永远一致。
# 2. 全部以台面坐标(240x320)书写,内部按 SS 倍超采样后再 LANCZOS 缩回,
#    线条/圆/弧才有抗锯齿 —— 原版是预渲染 3D 图,平滑观感是"像不像"的关键。
# 3. 分层作画:深空底 → 装饰 → 发光 → 护板 → 金属导轨 → 记分板边框,
#    每一层都对应原版台面的一个物理层次。
import math
import random
import re
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

W, H = 240, 320
SS = 3
DW, DH = W * SS, H * SS

# ---- 调色板(对标原版:深空底 + 铬合金导轨 + 黄挡板 + 红 bumper) ----
COL_CABINET = (6, 8, 14)
COL_BED_TOP = (14, 26, 58)
COL_BED_BOT = (6, 11, 26)
COL_RAIL_HI = (214, 230, 244)
COL_RAIL_MID = (116, 133, 152)
COL_RAIL_LO = (34, 44, 58)
COL_RAIL_EDGE = (12, 17, 26)
COL_BUMPER_SKIRT = (196, 42, 34)
COL_BUMPER_SKIRT_D = (104, 16, 16)
COL_BUMPER_CAP = (232, 236, 242)
COL_BUMPER_LIT = (255, 214, 120)
COL_SLING_RUBBER = (214, 62, 40)
COL_SLING_PLASTIC = (250, 138, 46)
COL_TARGET_UP = (226, 240, 232)
COL_TARGET_EDGE = (46, 168, 96)
COL_LENS_OFF = (44, 40, 24)
COL_LENS_ON = (255, 226, 120)
COL_FLIP_BODY = (252, 200, 46)
COL_FLIP_HI = (255, 240, 170)
COL_FLIP_LO = (196, 124, 10)
COL_FLIP_EDGE = (94, 56, 4)
COL_BALL = (226, 232, 240)
COL_APRON = (18, 24, 38)
COL_APRON_HI = (44, 56, 78)
COL_BEZEL = (10, 13, 20)
COL_PANEL_EDGE = (58, 70, 90)
COL_PLASTIC = (26, 52, 96)
COL_PLASTIC_HI = (96, 158, 220)
COL_GOLD = (226, 186, 92)

# 5x7 点阵字体(bit0 = 顶行),台面招牌用。
FONT5X7 = {
    ' ': (0, 0, 0, 0, 0), '!': (0, 0, 0x5F, 0, 0), '-': (0, 0x08, 0x08, 0, 0),
    '.': (0, 0x60, 0x60, 0, 0), '%': (0x23, 0x13, 0x08, 0x64, 0x62),
    '/': (0x20, 0x10, 0x08, 0x04, 0x02), ':': (0, 0x36, 0x36, 0, 0),
    '0': (0x3E, 0x51, 0x49, 0x45, 0x3E), '1': (0x00, 0x42, 0x7F, 0x40, 0x00),
    '2': (0x42, 0x61, 0x51, 0x49, 0x46), '3': (0x21, 0x41, 0x45, 0x4B, 0x31),
    '4': (0x18, 0x14, 0x12, 0x7F, 0x10), '5': (0x27, 0x45, 0x45, 0x45, 0x39),
    '6': (0x3C, 0x4A, 0x49, 0x49, 0x30), '7': (0x01, 0x71, 0x09, 0x05, 0x03),
    '8': (0x36, 0x49, 0x49, 0x49, 0x36), '9': (0x06, 0x49, 0x49, 0x29, 0x1E),
    'A': (0x7E, 0x11, 0x11, 0x11, 0x7E), 'B': (0x7F, 0x49, 0x49, 0x49, 0x36),
    'C': (0x3E, 0x41, 0x41, 0x41, 0x22), 'D': (0x7F, 0x41, 0x22, 0x14, 0x08),
    'E': (0x7F, 0x49, 0x49, 0x49, 0x41), 'F': (0x7F, 0x09, 0x09, 0x09, 0x01),
    'G': (0x3E, 0x41, 0x49, 0x49, 0x7A), 'H': (0x7F, 0x08, 0x08, 0x08, 0x7F),
    'I': (0x00, 0x41, 0x7F, 0x41, 0x00), 'J': (0x20, 0x40, 0x41, 0x3F, 0x01),
    'K': (0x7F, 0x08, 0x14, 0x22, 0x41), 'L': (0x7F, 0x40, 0x40, 0x40, 0x40),
    'M': (0x7F, 0x02, 0x0C, 0x02, 0x7F), 'N': (0x7F, 0x04, 0x08, 0x10, 0x7F),
    'O': (0x3E, 0x41, 0x41, 0x41, 0x3E), 'P': (0x7F, 0x09, 0x09, 0x09, 0x06),
    'Q': (0x3E, 0x41, 0x51, 0x21, 0x5E), 'R': (0x7F, 0x09, 0x19, 0x29, 0x46),
    'S': (0x46, 0x49, 0x49, 0x49, 0x31), 'T': (0x01, 0x01, 0x7F, 0x01, 0x01),
    'U': (0x3F, 0x40, 0x40, 0x40, 0x3F), 'V': (0x1F, 0x20, 0x40, 0x20, 0x1F),
    'W': (0x3F, 0x40, 0x38, 0x40, 0x3F), 'X': (0x63, 0x14, 0x08, 0x14, 0x63),
    'Y': (0x07, 0x08, 0x70, 0x08, 0x07), 'Z': (0x61, 0x51, 0x49, 0x45, 0x43),
}


# ---------------------------------------------------------------------------
# 几何解析
# ---------------------------------------------------------------------------

_NUM = r"(-?[\d.]+)f?"

# 表达式求值只允许这些字符:数字、小数点、pi、四则运算、空白。
_EXPR_OK = re.compile(r"^[0-9pi\s+\-*/.()]+$")


def eval_num(s):
    """把 C 里的常量表达式算成 float。支持 (float) 强转、M_PI、float 后缀 f。"""
    t = re.sub(r"//[^\n]*", "", s).strip()
    t = t.replace("(float)", " ").replace("M_PI", "pi").replace("PI", "pi")
    t = re.sub(r"([0-9.])f(?![a-zA-Z_])", r"\1", t)
    if not _EXPR_OK.match(t):
        sys.exit("parse: 无法解析数值表达式 %r" % s)
    return float(eval(t, {"__builtins__": {}}, {"pi": math.pi}))  # noqa: S307


def parse_geometry(path):
    """从 pb_table.c 里解析台面几何。C 文件结构变了这里会直接报错,不会静默画歪。"""
    src = open(path, "r", encoding="utf-8").read()
    nocmt = re.sub(r"//[^\n]*", "", src)

    m = re.search(r"seg_def_t\s+SEGS\[\]\s*=\s*\{(.*?)\n\};", nocmt, re.S)
    if not m:
        sys.exit("parse: 找不到 SEGS 定义")
    row = re.compile(r"\{\s*" + _NUM + r"\s*,\s*" + _NUM + r"\s*,\s*" + _NUM +
                     r"\s*,\s*" + _NUM + r"\s*,\s*(PB_SEG_\w+)\s*,\s*" + _NUM +
                     r"\s*,\s*" + _NUM + r"\s*,\s*(true|false)\s*\}")
    segs = []
    for mm in row.finditer(m.group(1)):
        x1, y1, x2, y2, kind, rest, kick, gate = mm.groups()
        segs.append(dict(a=(float(x1), float(y1)), b=(float(x2), float(y2)),
                         kind=kind, rest=float(rest), kick=float(kick),
                         gate=(gate == "true")))
    if len(segs) < 10:
        sys.exit("parse: SEGS 只解析到 %d 条,正则失配" % len(segs))

    circles = []
    m = re.search(r"pb_circle\s+CIRCLES\[\]\s*=\s*\{(.*?)\n\};", nocmt, re.S)
    if m:
        crow = re.compile(r"\{\s*\{\s*" + _NUM + r"\s*,\s*" + _NUM + r"\s*\}\s*,\s*" +
                          _NUM + r"\s*,\s*" + _NUM + r"\s*,\s*" + _NUM +
                          r"\s*,\s*(true|false)\s*\}")
        for mm in crow.finditer(m.group(1)):
            cx, cy, r, _rest, _kick, _solid = mm.groups()
            circles.append(dict(c=(float(cx), float(cy)), r=float(r)))

    flippers = []
    for mm in re.finditer(r"init_flipper\(&t->flippers\[(\d)\]\s*,\s*([^,]+),\s*"
                          r"([^,]+),\s*([^,]+),\s*([^;]+)\)\s*;", nocmt):
        idx, px, py, rest, raised = mm.groups()
        flippers.append(dict(idx=int(idx), pivot=(eval_num(px), eval_num(py)),
                             rest=eval_num(rest), raised=eval_num(raised)))
    if len(flippers) != 2:
        sys.exit("parse: 挡板应解析到 2 个,实际 %d" % len(flippers))
    mlen = re.search(r"f->len\s*=\s*([^;]+);", nocmt)
    flen = eval_num(mlen.group(1)) if mlen else 34.0

    lanes = [eval_num(v) for v in re.findall(r"t->lane_x\[\d\]\s*=\s*([^;]+);", nocmt)]
    mly = re.search(r"t->lane_y\s*=\s*([^;]+);", nocmt)
    lane_y = eval_num(mly.group(1)) if mly else 42.0
    mbr = re.search(r"#define\s+PB_BALL_R\s+([^\n]+)", src)
    ball_r = eval_num(mbr.group(1)) if mbr else 4.0

    def segs_of(kind):
        return [s for s in segs if s["kind"] == kind]

    return dict(segs=segs, circles=circles, flippers=flippers, flip_len=flen,
                lanes=lanes, lane_y=lane_y, ball_r=ball_r,
                walls=segs_of("PB_SEG_WALL"), slings=segs_of("PB_SEG_SLING"),
                targets=segs_of("PB_SEG_TARGET"), gates=segs_of("PB_SEG_GATE"),
                floors=segs_of("PB_SEG_FLOOR"))


# ---------------------------------------------------------------------------
# 绘图工具
# ---------------------------------------------------------------------------

def mix(a, b, t):
    t = max(0.0, min(1.0, t))
    return (int(round(a[0] + (b[0] - a[0]) * t)),
            int(round(a[1] + (b[1] - a[1]) * t)),
            int(round(a[2] + (b[2] - a[2]) * t)))


def sw(v):
    return max(1, int(round(v * SS)))


def blur(img, r):
    return img.filter(ImageFilter.GaussianBlur(max(0.1, r * SS)))


def seg_normal(a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    L = math.hypot(dx, dy) or 1.0
    nx, ny = -dy / L, dx / L
    if nx + ny > 0:              # 统一取指向左上的法向(光源在左上)
        nx, ny = -nx, -ny
    return nx, ny


class Painter:
    """ImageDraw 的台面坐标包装。

    ox/oy:坐标偏移。画精灵时传 (-bbox_x0, -bbox_y0),draw 函数就能继续用
    "锚点坐标系"书写,不用关心精灵自己被裁到了哪个包围盒。
    """

    def __init__(self, img, ox=0.0, oy=0.0):
        self.img = img
        self.ox = ox
        self.oy = oy
        self.d = ImageDraw.Draw(img)

    def _p(self, x, y):
        return ((x + self.ox) * SS, (y + self.oy) * SS)

    def line(self, ps, fill, w=1.0, joint="curve"):
        pts = [self._p(x, y) for x, y in ps]
        self.d.line(pts, fill=fill, width=sw(w),
                    joint=joint if len(pts) > 2 else None)

    def poly(self, ps, fill=None, outline=None, w=1.0):
        pts = [self._p(x, y) for x, y in ps]
        if outline is not None:
            self.d.polygon(pts, fill=fill, outline=outline, width=sw(w))
        else:
            self.d.polygon(pts, fill=fill)

    def rect(self, x0, y0, x1, y1, fill=None, outline=None, w=1.0):
        a = self._p(x0, y0)
        b = self._p(x1, y1)
        self.d.rectangle([a[0], a[1], b[0], b[1]], fill=fill, outline=outline,
                         width=sw(w) if outline else 1)

    def ell(self, x0, y0, x1, y1, fill=None, outline=None, w=1.0):
        a = self._p(x0, y0)
        b = self._p(x1, y1)
        self.d.ellipse([a[0], a[1], b[0], b[1]], fill=fill, outline=outline,
                       width=sw(w) if outline else 1)

    def circle(self, cx, cy, r, **kw):
        self.ell(cx - r, cy - r, cx + r, cy + r, **kw)

    def arc(self, x0, y0, x1, y1, a0, a1, fill, w=1.0):
        a = self._p(x0, y0)
        b = self._p(x1, y1)
        self.d.arc([a[0], a[1], b[0], b[1]], a0, a1, fill=fill, width=sw(w))

    def text(self, x, y, s, fill, scale=1.0, spacing=1.0):
        step = (5 + spacing) * scale
        for i, ch in enumerate(s.upper()):
            g = FONT5X7.get(ch, FONT5X7[' '])
            for col in range(5):
                bits = g[col]
                for row in range(7):
                    if (bits >> row) & 1:
                        px, py = self._p(x + i * step + col * scale, y + row * scale)
                        self.d.rectangle([px, py, px + scale * SS - 1,
                                          py + scale * SS - 1], fill=fill)
        return len(s) * step - spacing * scale

    def text_w(self, s, scale=1.0, spacing=1.0):
        return len(s) * (5 + spacing) * scale - spacing * scale

    def text_c(self, cx, y, s, fill, scale=1.0, spacing=1.0):
        self.text(cx - self.text_w(s, scale, spacing) / 2.0, y, s, fill, scale, spacing)

    def text_arc(self, cx, cy, radius, s, a0_deg, a1_deg, fill, scale=1.0):
        n = len(s)
        for i, ch in enumerate(s.upper()):
            t = i / (n - 1) if n > 1 else 0.5
            a = math.radians(a0_deg + (a1_deg - a0_deg) * t)
            x = cx + math.cos(a) * radius
            y = cy + math.sin(a) * radius
            g = FONT5X7.get(ch, FONT5X7[' '])
            ca, sa = math.cos(a + math.pi / 2), math.sin(a + math.pi / 2)
            hs = scale * 0.62
            for col in range(5):
                bits = g[col]
                for row in range(7):
                    if (bits >> row) & 1:
                        lx, ly = (col - 2) * scale, (row - 3) * scale
                        px, py = self._p(x + lx * ca - ly * sa, y + lx * sa + ly * ca)
                        self.d.ellipse([px - hs * SS, py - hs * SS,
                                        px + hs * SS, py + hs * SS], fill=fill)


class MaskPainter(Painter):
    """把任何颜色强制成白色的 Painter。

    精灵的 alpha 通道靠它生成:同一个 draw 函数跑两遍,一遍出 RGB、一遍出
    轮廓掩膜,就不用为每个精灵写两份几何代码,也保证两者像素级一致。
    """

    W = (255, 255, 255)

    def line(self, ps, fill, w=1.0, joint="curve"):
        super().line(ps, self.W, w, joint)

    def poly(self, ps, fill=None, outline=None, w=1.0):
        super().poly(ps, self.W if fill is not None else None,
                     self.W if outline is not None else None, w)

    def rect(self, x0, y0, x1, y1, fill=None, outline=None, w=1.0):
        super().rect(x0, y0, x1, y1, self.W if fill is not None else None,
                     self.W if outline is not None else None, w)

    def ell(self, x0, y0, x1, y1, fill=None, outline=None, w=1.0):
        super().ell(x0, y0, x1, y1, self.W if fill is not None else None,
                    self.W if outline is not None else None, w)

    def arc(self, x0, y0, x1, y1, a0, a1, fill, w=1.0):
        super().arc(x0, y0, x1, y1, a0, a1, self.W, w)

    def text(self, x, y, s, fill, scale=1.0, spacing=1.0):
        return super().text(x, y, s, self.W, scale, spacing)

    def text_c(self, cx, y, s, fill, scale=1.0, spacing=1.0):
        super().text_c(cx, y, s, self.W, scale, spacing)

    def text_arc(self, cx, cy, radius, s, a0_deg, a1_deg, fill, scale=1.0):
        super().text_arc(cx, cy, radius, s, a0_deg, a1_deg, self.W, scale)


def mask_from_poly(polys):
    m = Image.new("L", (DW, DH), 0)
    d = ImageDraw.Draw(m)
    for p in polys:
        d.polygon([(x * SS, y * SS) for x, y in p], fill=255)
    return m


def draw_rail(pt, segs, w=4.2, hi=COL_RAIL_HI, mid=COL_RAIL_MID, lo=COL_RAIL_LO,
              edge=COL_RAIL_EDGE):
    """金属导轨:投影 → 暗边 → 主体 → 高光,四道描边叠出圆柱金属感。"""
    for s in segs:
        a, b = s["a"], s["b"]
        nx, ny = seg_normal(a, b)
        pt.line([(a[0] - nx * 1.7, a[1] - ny * 1.7), (b[0] - nx * 1.7, b[1] - ny * 1.7)],
                (6, 8, 14), w + 2.8)
        pt.line([a, b], edge, w + 1.5)
        pt.line([a, b], lo, w + 0.5)
        pt.line([a, b], mid, w - 0.9)
        pt.line([(a[0] + nx * w * 0.26, a[1] + ny * w * 0.26),
                 (b[0] + nx * w * 0.26, b[1] + ny * w * 0.26)],
                hi, max(0.7, w * 0.24))


# ---------------------------------------------------------------------------
# 台面区域定义(与 pb_table.c 的外墙折线一致)
# ---------------------------------------------------------------------------

BED_POLY = [
    (72, 277), (10, 264), (10, 96), (14, 56), (34, 32), (70, 25), (150, 25),
    (190, 28), (204, 44), (204, 264), (142, 277), (142, 320), (72, 320),
]
LANE_POLY = [(204, 50), (226, 42), (228, 40), (230, 70), (230, 306), (204, 306)]
APRON_L = [(10, 264), (72, 277), (72, 320), (10, 320)]
APRON_R = [(204, 264), (142, 277), (142, 320), (204, 320)]
DRAIN_BOX = (72, 277, 142, 320)
BEZEL_H = 26

# 记分板预留区:LVGL 标签贴在这三块暗面板上(见 pb_render.c)
PANEL_SCORE = (5, 110)
PANEL_MULT = (114, 150)
PANEL_BALL = (154, 235)


def paint_backdrop():
    """深空底:竖向渐变 + 星云 + 星点 + 中央顶光。"""
    grad = Image.new("RGB", (DW, DH))
    gd = ImageDraw.Draw(grad)
    for y in range(DH):
        gd.line([(0, y), (DW, y)], fill=mix(COL_BED_TOP, COL_BED_BOT, (y / DH) ** 1.25))

    neb = Image.new("RGB", (DW, DH), (0, 0, 0))
    nd = ImageDraw.Draw(neb)
    nd.ellipse([-30 * SS, 40 * SS, 150 * SS, 210 * SS], (52, 26, 84))
    nd.ellipse([90 * SS, 120 * SS, 260 * SS, 300 * SS], (16, 54, 96))
    nd.ellipse([40 * SS, 200 * SS, 190 * SS, 330 * SS], (60, 24, 52))
    grad = ImageChops.screen(grad, blur(neb, 22))

    rnd = random.Random(20240917)
    stars = Image.new("RGB", (DW, DH), (0, 0, 0))
    sd = ImageDraw.Draw(stars)
    for _ in range(430):
        x, y = rnd.uniform(8, 206), rnd.uniform(24, 300)
        b = rnd.uniform(0.15, 1.0) ** 1.7
        r = 0.5 if b < 0.55 else (0.9 if b < 0.9 else 1.3)
        sd.ellipse([(x - r) * SS, (y - r) * SS, (x + r) * SS, (y + r) * SS],
                   fill=mix((10, 14, 30), (255, 252, 240), b))
    for _ in range(8):
        x, y = rnd.uniform(20, 200), rnd.uniform(30, 290)
        L = rnd.uniform(2.2, 4.0)
        sd.line([(x - L, y), (x + L, y)], fill=(170, 195, 230), width=sw(0.5))
        sd.line([(x, y - L), (x, y + L)], fill=(170, 195, 230), width=sw(0.5))
        sd.ellipse([(x - 1) * SS, (y - 1) * SS, (x + 1) * SS, (y + 1) * SS],
                   fill=(255, 255, 250))
    grad = ImageChops.screen(grad, blur(stars, 0.28))

    light = Image.new("L", (DW, DH), 0)
    ImageDraw.Draw(light).ellipse([-40 * SS, -60 * SS, 280 * SS, 300 * SS], fill=150)
    light = blur(light, 46).point(lambda v: min(120, int(v * 0.55)))
    return Image.composite(Image.new("RGB", (DW, DH), (58, 84, 132)), grad, light)


def _plastic_panel(pt, x0, y0, x1, y1, label, sub=None):
    # UI 盘点 #2:半透明深蓝底(#082060,RGB565 静态烘焙用同色系混合)+浅蓝边框,
    # 和星空台面背景明确区隔,不再像游戏美术的一部分。
    pt.poly([(x0, y0), (x1, y0), (x1, y1), (x0, y1)], (8, 32, 96))
    pt.poly([(x0 + 1, y0 + 1), (x1 - 1, y0 + 1), (x1 - 1, y0 + 2.4), (x0 + 1, y0 + 2.4)],
            (110, 170, 230))
    pt.rect(x0, y0, x1, y1, outline=(110, 170, 230), w=1.6)
    pt.rect(x0 + 2.0, y0 + 2.0, x1 - 2.0, y1 - 2.0,
            outline=(30, 60, 120), w=0.6)
    pt.text_c((x0 + x1) / 2.0, y0 + 6.0, label, (210, 232, 255), 1.0)
    if sub:
        pt.text_c((x0 + x1) / 2.0, y0 + 14.0, sub, COL_GOLD, 1.0)


def paint_decor(g, bed):
    """台面装饰:中央徽章(行星+火箭)、两侧塑料面板、招牌。

    这里不再画任何"灯插":徽章上方的菱形灯插与军衔进度环顶灯、小立柱精灵
    三重重叠(用户报"文字错乱"的位置),那块空档现在整条让给信息带。
    """
    pt = Painter(bed)

    cx, r = 107, 37
    cy = g["ring"][1]                       # 徽章圆心 = outer_circle 环心
    pt.circle(cx, cy, r + 3.0, fill=(8, 12, 22))
    pt.circle(cx, cy, r + 2.0, fill=COL_RAIL_LO)
    pt.circle(cx, cy, r + 1.0, fill=COL_RAIL_MID)
    pt.circle(cx, cy, r, fill=(9, 17, 38))

    halo = Image.new("RGB", (DW, DH), (0, 0, 0))
    hp = Painter(halo)
    hp.circle(cx - 8, cy + 4, 17, fill=(24, 62, 116))
    hp.circle(cx + 14, cy - 13, 9, fill=(96, 40, 96))
    hp.circle(cx - 16, cy - 14, 6, fill=(20, 70, 96))
    halo = blur(halo, 8)
    disc = Image.new("L", (DW, DH), 0)
    ImageDraw.Draw(disc).ellipse([(cx - r) * SS, (cy - r) * SS,
                                  (cx + r) * SS, (cy + r) * SS], fill=255)
    bed.paste(ImageChops.screen(bed, halo), (0, 0), disc)
    pt = Painter(bed)

    # 徽章内的行星 + 光环(光环后半段先画,前半段后画,才有环绕感)
    pcx, pcy, pr = cx - 3, cy + 6, 14
    ring = (pcx - pr - 8, pcy - pr - 8, pcx + pr + 8, pcy + pr + 8)
    pt.arc(*ring, 140, 340, mix(COL_GOLD, (255, 240, 190), 0.35), 1.8)
    pt.circle(pcx, pcy, pr, fill=(38, 96, 168))
    pt.circle(pcx - 4, pcy - 4, pr - 4, fill=(58, 128, 200))
    for i, yy in enumerate((-6, -1, 5)):
        pt.ell(pcx - pr + 1, pcy + yy, pcx + pr - 1, pcy + yy + 2.2,
               fill=(28, 76, 140) if i % 2 == 0 else (72, 148, 214))
    pt.circle(pcx - 5, pcy - 6, 3.4, fill=(150, 214, 158))
    pt.circle(pcx + 6, pcy + 3, 2.2, fill=(196, 168, 120))
    pt.arc(*ring, 320, 160, COL_GOLD, 1.8)
    pt.circle(pcx, pcy, pr, outline=(10, 20, 40), w=0.9)
    pt.arc(cx - r + 1.5, cy - r + 1.5, cx + r - 1.5, cy + r - 1.5, 195, 320,
           (120, 170, 220), 1.7)

    # UI 实机反馈:徽章内火箭压在行星光环与弧字上(小屏上三重重叠),整段删除,
    # 徽章保留"弧字 + 行星 + 金色光环"三要素。
    pt.text_arc(cx, cy + 2, r - 8.5, "SPACE CADET", 218, 322, (198, 226, 255), 0.85)
    # UI 盘点 #2:徽章底部的烘焙字 "MISSION READY" 删除——事件提示由动态信息带负责,
    # 烘焙字永远亮着且压在台面中央,与信息带职责重复(用户拍板删除)。

    _plastic_panel(pt, 16, 198, 58, 230, "ATTACK", None)   # 当前 bumper 档位分值
    _plastic_panel(pt, 156, 198, 198, 230, "RANK", None)    # 军衔由渲染层动态显示

    # UI 实机反馈:顶部 "SPACE PINBALL" 横幅(框+字)在小屏上模糊不请,
    # 且与状态栏/菜单标题职责重复,整块删除。

    for sx, sgn in ((30, 1), (184, -1)):        # 底部导轨旁的 outlane 箭头
        for i in range(3):
            x = sx + sgn * i * 7
            pt.poly([(x, 267), (x + sgn * 4, 270.5), (x, 274)],
                    mix((150, 180, 220), (24, 34, 52), i / 3.0))


def paint_lane_channel(g, img):
    """发球道:比台面更暗的通道 + 装饰文字(弹簧是精灵之外的静态件)。"""
    grad = Image.new("RGB", (DW, DH))
    gd = ImageDraw.Draw(grad)
    for x in range(204 * SS, 231 * SS):
        t = (x / SS - 204) / 26.0
        gd.line([(x, 0), (x, DH)],
                fill=mix((22, 30, 50), (7, 11, 20), 0.5 + 0.5 * math.sin(t * math.pi)))
    img.paste(grad, (0, 0), mask_from_poly([LANE_POLY]))

    pt = Painter(img)
    for i in range(3):
        y = 132 + i * 17
        pt.poly([(217, y - 6), (222, y + 2.5), (212, y + 2.5)],
                mix(COL_GOLD, (52, 36, 10), i / 3.0))
    # UI 盘点 #2:竖排 HOLD/FIRE 删除(通道净宽 22px 放不下横排,竖排难读;
    # 发射指示由上方三枚金色箭头 + 蓝色蓄力条精灵承担,用户拍板删除)。


def paint_static_hardware(g, img):
    """静态硬件:单向阀丝、弹弓塑料面、目标凹槽、灯环、bumper 裙与铬环。

    会变化的部分(挡板/球/灯芯/靶板/橡皮筋/蓄力条)不画在这里,由精灵负责。
    """
    pt = Painter(img)

    for s in g["gates"]:                        # 单向阀:细铬丝
        pt.line([s["a"], s["b"]], (14, 20, 32), 2.8)
        pt.line([s["a"], s["b"]], COL_RAIL_MID, 1.3)
        for t in (0.22, 0.5, 0.78):
            x = s["a"][0] + (s["b"][0] - s["a"][0]) * t
            y = s["a"][1] + (s["b"][1] - s["a"][1]) * t
            pt.circle(x, y, 1.2, fill=COL_RAIL_HI)

    for s in g["slings"]:                       # 弹弓塑料三角面
        a, b = s["a"], s["b"]
        nbr = [o for o in g["segs"] if o is not s and
               (o["a"] == a or o["b"] == a or o["a"] == b or o["b"] == b)]
        cnt = {}
        for o in nbr:
            for v in (o["a"], o["b"]):
                cnt[v] = cnt.get(v, 0) + 1
        apex = max(cnt, key=lambda v: cnt[v]) if cnt else a
        poly = [a, b, apex]
        pt.poly(poly, mix(COL_SLING_PLASTIC, (40, 16, 8), 0.42))
        pt.poly(poly, outline=(10, 12, 18), w=0.9)
        mid = ((a[0] + b[0] + apex[0]) / 3, (a[1] + b[1] + apex[1]) / 3)
        edge = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
        pt.line([mid, edge], mix(COL_SLING_PLASTIC, (255, 226, 150), 0.5), 1.8)
        for t in (0.3, 0.7):                     # 面上的两道闪电纹
            px = apex[0] + (edge[0] - apex[0]) * t
            py = apex[1] + (edge[1] - apex[1]) * t
            pt.poly([(px, py - 3), (px + 2.2, py), (px - 0.6, py + 0.4),
                     (px + 0.8, py + 4)], mix(COL_SLING_PLASTIC, (255, 255, 220), 0.6))

    for s in g["targets"]:                      # 掉落目标:凹槽(靶板是精灵)
        a, b = s["a"], s["b"]
        nx, ny = seg_normal(a, b)
        pt.line([(a[0] - nx * 3.4, a[1] - ny * 3.4), (b[0] - nx * 3.4, b[1] - ny * 3.4)],
                (5, 8, 13), 7.4)
        pt.line([(a[0] - nx * 3.4, a[1] - ny * 3.4), (b[0] - nx * 3.4, b[1] - ny * 3.4)],
                (26, 34, 48), 6.0)

    for lx in g["lanes"]:                       # 车道灯:金属环 + 凹槽(灯芯是精灵)
        pt.ell(lx - 8.0, g["lane_y"] - 6.0, lx + 8.0, g["lane_y"] + 6.0, fill=(6, 10, 18))
        pt.ell(lx - 7.2, g["lane_y"] - 5.2, lx + 7.2, g["lane_y"] + 5.2,
               outline=COL_RAIL_MID, w=0.9)
        pt.ell(lx - 6.4, g["lane_y"] - 4.4, lx + 6.4, g["lane_y"] + 4.4, fill=(12, 14, 20))

    for c in g["circles"]:                      # pop bumper:投影 + 红裙 + 铬环
        cx, cy, r = c["c"][0], c["c"][1], c["r"]
        pt.circle(cx + 1.5, cy + 2.2, r + 1.8, fill=(4, 6, 12))
        pt.circle(cx, cy, r + 1.3, fill=COL_BUMPER_SKIRT_D)
        pt.circle(cx, cy, r, fill=COL_BUMPER_SKIRT)
        pt.arc(cx - r, cy - r, cx + r, cy + r, 150, 330, (255, 128, 100), r * 0.22)
        pt.circle(cx, cy, r * 0.66, fill=(18, 22, 32))
        pt.circle(cx, cy, r * 0.60, fill=COL_RAIL_LO)

    # 发球道底部:弹簧座 + 推杆(蓄力条精灵叠在上面)
    for i in range(6):
        y = 288 + i * 3.0
        pt.line([(208, y), (226, y + 1.7)], mix(COL_RAIL_MID, COL_RAIL_LO, i / 6.0), 1.6)
    pt.rect(206, 283, 228, 288, fill=COL_RAIL_MID, outline=COL_RAIL_EDGE, w=0.6)
    pt.rect(206.8, 283.8, 227.2, 285.2, fill=COL_RAIL_HI)


def paint_apron_and_drain(img):
    """底部护板 + 落球洞:实体塑料件,盖在台面床之上。"""
    pt = Painter(img)
    x0, y0, x1, y1 = DRAIN_BOX
    pt.rect(x0, y0, x1, y1, fill=(4, 6, 12))
    pt.ell(107 - 28, 305 - 11, 107 + 28, 305 + 11, fill=(0, 0, 0))
    pt.arc(107 - 28, 305 - 11, 107 + 28, 305 + 11, 180, 360, COL_RAIL_LO, 1.5)
    pt.arc(107 - 28, 305 - 11, 107 + 28, 305 + 11, 0, 180, (70, 84, 104), 1.5)
    pt.ell(107 - 19, 307 - 7, 107 + 19, 307 + 7, fill=(0, 0, 0))

    for poly in (APRON_L, APRON_R):
        pt.poly(poly, COL_APRON)
        pt.line([poly[0], poly[1]], COL_APRON_HI, 1.6)
        pt.line([(poly[0][0], poly[0][1] + 1.8), (poly[1][0], poly[1][1] + 1.8)],
                mix(COL_APRON, (0, 0, 0), 0.55), 1.0)
        pt.poly(poly, outline=(6, 9, 16), w=0.9)
    pt.text_c(38, 293, "OUT", (96, 116, 146), 1.0)
    pt.text_c(176, 293, "OUT", (96, 116, 146), 1.0)


def paint_glow_and_vignette(g, img):
    glow = Image.new("RGB", (DW, DH), (0, 0, 0))
    gp = Painter(glow)
    gp.circle(107, g["ring"][1], 41, fill=(20, 38, 74))
    gp.rect(62, 57, 152, 68, fill=(18, 32, 58))
    gp.rect(16, 198, 58, 230, fill=(14, 26, 50))
    gp.rect(156, 198, 198, 230, fill=(14, 26, 50))
    glow = blur(glow, 6.0)
    img.paste(ImageChops.screen(img, glow), (0, 0))

    vig = Image.new("L", (DW, DH), 0)
    ImageDraw.Draw(vig).ellipse([-6 * SS, -8 * SS, (W + 6) * SS, (H + 8) * SS],
                                fill=255)
    vig = blur(vig, 40).point(lambda v: min(150, int((255 - v) * 0.72)))
    img.paste(Image.new("RGB", (DW, DH), (0, 0, 0)), (0, 0), vig)


def paint_bezel(img):
    """顶部记分板边框,面板尺寸与 PANEL_* 常量一致。"""
    pt = Painter(img)
    pt.rect(0, 0, W, BEZEL_H - 1, fill=COL_BEZEL)
    for x in range(0, W, 3):
        pt.line([(x, 1), (x, BEZEL_H - 2)], mix(COL_BEZEL, (255, 255, 255), 0.025), 0.5)

    for x0, x1 in (PANEL_SCORE, PANEL_MULT, PANEL_BALL):
        pt.rect(x0, 4, x1, 22, fill=(2, 5, 10))
        pt.rect(x0, 4, x1, 22, outline=(28, 38, 54), w=1.0)
        pt.rect(x0 + 0.9, 4.9, x1 - 0.9, 21.1, outline=COL_PANEL_EDGE, w=0.6)
        pt.rect(x0 + 1.4, 5.4, x1 - 1.4, 7.4, fill=(1, 3, 7))
        for gx in range(x0 + 3, x1 - 2, 4):      # LCD 网格感
            pt.line([(gx, 8), (gx, 20)], (10, 18, 28), 0.4)

    pt.line([(0, 0.5), (W, 0.5)], (78, 92, 112), 1.0)
    pt.line([(0, BEZEL_H - 1.4), (W, BEZEL_H - 1.4)], (72, 86, 106), 1.2)
    pt.line([(0, BEZEL_H - 0.2), (W, BEZEL_H - 0.2)], (3, 5, 9), 1.2)
    for x in (3, W - 3):
        pt.circle(x, 13, 1.6, fill=(48, 58, 74))
        pt.circle(x - 0.4, 12.5, 0.6, fill=(120, 136, 156))


def render_background(g):
    """渲染整张台面背景,返回 240x320 RGB。"""
    img = Image.new("RGB", (DW, DH), COL_CABINET)
    rnd = random.Random(7)
    nd = ImageDraw.Draw(img)
    for _ in range(2800):                        # 机柜外框的细噪点
        nd.point((rnd.randrange(DW), rnd.randrange(DH)),
                 fill=mix(COL_CABINET, (255, 255, 255), rnd.uniform(0, 0.05)))

    bed = paint_backdrop()
    paint_decor(g, bed)
    img.paste(bed, (0, 0), mask_from_poly([BED_POLY]))

    paint_lane_channel(g, img)
    paint_static_hardware(g, img)
    paint_side_holes(g, img)
    paint_ring_lamps(g, img)
    paint_upgrade_lamps(g, img)
    paint_apron_and_drain(img)
    paint_black_hole(g, img)          # 落球口里的黑洞要压在护板之上
    paint_info_strip(g, img)          # 信息带压在所有塑料件之上(文字底板)
    paint_glow_and_vignette(g, img)

    draw_rail(Painter(img), g["walls"], 4.2)     # 导轨压在最上层
    for s in g["floors"]:
        draw_rail(Painter(img), [s], 3.4)
    paint_bezel(img)
    return img.resize((W, H), Image.LANCZOS)


# ---------------------------------------------------------------------------
# 精灵
#
# 分工:背景只画静态的凹槽/塑料件/铬环,会变的部分(球、挡板、灯芯、靶板、
# 橡皮筋、bumper 帽)全部做成精灵,运行时 lv_image_set_src 换帧即可。
# ---------------------------------------------------------------------------

FLIP_FRAMES = 12


def make_sprite(draw_fn, x0, y0, x1, y1):
    """把 draw_fn 画到包围盒 (x0,y0)-(x1,y1),返回带 alpha 的精灵。

    同一个 draw_fn 跑两遍:一遍出 RGB(黑底),一遍经 MaskPainter 出白色轮廓掩膜。
    缩回后 RGB 边缘会因为混入黑底而变暗,所以再按 alpha 反预乘回去,
    避开 LVGL 非预乘混合时的黑边。
    """
    ix0, iy0 = int(math.floor(x0)), int(math.floor(y0))
    ix1, iy1 = int(math.ceil(x1)), int(math.ceil(y1))
    w, h = ix1 - ix0, iy1 - iy0
    if w <= 0 or h <= 0:
        raise ValueError("sprite box empty: %r" % ((x0, y0, x1, y1),))
    rgb = Image.new("RGB", (w * SS, h * SS), (0, 0, 0))
    msk = Image.new("RGB", (w * SS, h * SS), (0, 0, 0))
    draw_fn(Painter(rgb, -ix0, -iy0))
    draw_fn(MaskPainter(msk, -ix0, -iy0))
    return dict(w=w, h=h, ox=ix0, oy=iy0,
                rgb=rgb.resize((w, h), Image.LANCZOS),
                a=msk.resize((w, h), Image.LANCZOS).convert("L"))


def sprite_to_rgb565a8(sp):
    """RGB565A8 布局:w*h*2 字节 RGB565(小端) + w*h 字节 alpha。"""
    rgb = sp["rgb"].convert("RGB").tobytes()
    a = sp["a"].tobytes()
    n = sp["w"] * sp["h"]
    out = bytearray(n * 2 + n)
    for i in range(n):
        av = a[i]
        r, gg, b = rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]
        if 0 < av < 255:                       # 反预乘:除回覆盖率,去黑边
            f = 255.0 / av
            r = min(255, int(r * f + 0.5))
            gg = min(255, int(gg * f + 0.5))
            b = min(255, int(b * f + 0.5))
        v = ((r & 0xF8) << 8) | ((gg & 0xFC) << 3) | (b >> 3)
        out[i * 2] = v & 0xFF
        out[i * 2 + 1] = v >> 8
        out[n * 2 + i] = av
    return bytes(out)


def image_to_rgb565(img):
    data = img.convert("RGB").tobytes()
    n = len(data) // 3
    out = bytearray(n * 2)
    for i in range(n):
        r, g, b = data[i * 3], data[i * 3 + 1], data[i * 3 + 2]
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        out[i * 2] = v & 0xFF
        out[i * 2 + 1] = v >> 8
    return bytes(out)


def _sphere(pt, cx, cy, r, dark, light, steps=10):
    """用递层偏心圆堆出金属球面渐变(光源左上)。"""
    for i in range(steps, 0, -1):
        t = i / steps
        rad = r * t + 0.2
        pt.circle(cx - (1 - t) * r * 0.48, cy - (1 - t) * r * 0.54, rad,
                  fill=mix(dark, light, (1 - t) ** 1.25))


def draw_ball(pt, r):
    pt.circle(1.0, 1.6, r + 0.5, fill=(3, 5, 10))
    _sphere(pt, 0, 0, r, (78, 86, 104), (252, 253, 255), 11)
    pt.circle(0, 0, r, outline=(196, 208, 224), w=0.45)
    pt.circle(-r * 0.34, -r * 0.38, r * 0.26, fill=(255, 255, 255))
    pt.circle(r * 0.30, r * 0.34, r * 0.20, fill=(150, 164, 188))


def flip_quad(theta, L, r0, r1):
    nx, ny = -math.sin(theta), math.cos(theta)
    tx, ty = L * math.cos(theta), L * math.sin(theta)
    return [(nx * r0, ny * r0), (tx + nx * r1, ty + ny * r1),
            (tx - nx * r1, ty - ny * r1), (-nx * r0, -ny * r0)], (tx, ty), (nx, ny)


def draw_flipper(pt, theta, L):
    outer, _tip, _n = flip_quad(theta, L + 0.7, 5.5, 3.1)
    pt.poly([(x + 1.4, y + 1.9) for x, y in outer], (6, 6, 3))
    pt.poly(outer, COL_FLIP_EDGE)
    body, tip, n = flip_quad(theta, L, 4.6, 2.3)
    pt.poly(body, COL_FLIP_BODY)
    pt.line([(n[0] * 3.0, n[1] * 3.0), (tip[0] + n[0] * 1.3, tip[1] + n[1] * 1.3)],
            COL_FLIP_HI, 1.6)
    pt.line([(-n[0] * 3.4, -n[1] * 3.4), (tip[0] - n[0] * 1.5, tip[1] - n[1] * 1.5)],
            COL_FLIP_LO, 1.7)
    pt.circle(tip[0], tip[1], 2.3, fill=mix(COL_FLIP_BODY, COL_FLIP_HI, 0.4))
    pt.circle(0, 0, 3.4, fill=COL_RAIL_LO)
    pt.circle(0, 0, 2.6, fill=COL_RAIL_MID)
    pt.circle(-0.5, -0.7, 1.4, fill=COL_RAIL_HI)
    pt.circle(0, 0, 0.8, fill=(22, 28, 38))


def flipper_bbox(theta, L):
    outer, _t, _n = flip_quad(theta, L + 0.7, 5.5, 3.1)
    xs = [p[0] for p in outer] + [p[0] + 1.4 for p in outer] + [-3.6, 3.6]
    ys = [p[1] for p in outer] + [p[1] + 1.9 for p in outer] + [-3.6, 3.6]
    return min(xs) - 0.6, min(ys) - 0.6, max(xs) + 0.6, max(ys) + 0.6


def draw_bumper_cap(pt, cx, cy, r, lit):
    cr = r * 0.60
    if lit:
        pt.circle(cx, cy, cr + 3.2, fill=(126, 68, 14))
        pt.circle(cx, cy, cr + 1.9, fill=(255, 188, 86))
    _sphere(pt, cx, cy, cr,
            (138, 112, 52) if lit else (126, 134, 150),
            (255, 250, 226) if lit else (250, 252, 255), 9)
    pt.circle(cx, cy, cr, outline=(38, 44, 56), w=0.6)
    pt.circle(cx - cr * 0.32, cy - cr * 0.36, cr * 0.24, fill=(255, 255, 255))
    if lit:
        pt.circle(cx, cy, cr * 0.42, fill=(255, 248, 214))


def draw_lens(pt, cx, cy, on):
    rx, ry = 6.2, 4.2
    if on:
        pt.ell(cx - rx - 2.2, cy - ry - 2.2, cx + rx + 2.2, cy + ry + 2.2,
               fill=(112, 78, 14))
        pt.ell(cx - rx - 1.0, cy - ry - 1.0, cx + rx + 1.0, cy + ry + 1.0,
               fill=(255, 192, 76))
    pt.ell(cx - rx, cy - ry, cx + rx, cy + ry,
           fill=COL_LENS_ON if on else COL_LENS_OFF)
    pt.ell(cx - rx + 0.9, cy - ry + 0.8, cx + rx - 0.9, cy + ry - 0.9,
           fill=mix(COL_LENS_ON, (255, 255, 255), 0.5) if on
           else mix(COL_LENS_OFF, (255, 255, 255), 0.10))
    pt.ell(cx - rx * 0.48, cy - ry * 0.58, cx + rx * 0.10, cy - ry * 0.02,
           fill=(255, 255, 252) if on else (104, 98, 68))
    pt.ell(cx - rx, cy - ry, cx + rx, cy + ry, outline=(14, 18, 26), w=0.5)


def draw_target(pt, a, b, up):
    mid = ((a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0)
    la = (a[0] - mid[0], a[1] - mid[1])
    lb = (b[0] - mid[0], b[1] - mid[1])
    nx, ny = seg_normal(a, b)

    def off(p, d):
        return (mid[0] + p[0] + nx * d, mid[1] + p[1] + ny * d)

    if not up:                       # 放倒:只剩贴在槽底的一条暗残片
        pt.poly([off(la, -3.0), off(lb, -3.0), off(lb, -1.2), off(la, -1.2)],
                (34, 44, 58))
        pt.poly([off(la, -2.6), off(lb, -2.6), off(lb, -1.8), off(la, -1.8)],
                (18, 24, 34))
        return
    plate = [off(la, 2.8), off(lb, 2.8), off(lb, -1.6), off(la, -1.6)]
    pt.poly([(x + 1.2, y + 1.6) for x, y in plate], (5, 8, 12))
    pt.poly(plate, COL_TARGET_EDGE)
    inner = [off(la, 2.2), off(lb, 2.2), off(lb, -0.9), off(la, -0.9)]
    pt.poly(inner, COL_TARGET_UP)
    pt.line([off(la, 1.9), off(lb, 1.9)], (255, 255, 255), 0.9)
    pt.line([off(la, -0.6), off(lb, -0.6)], mix(COL_TARGET_UP, COL_TARGET_EDGE, 0.6), 0.8)


def draw_sling(pt, a, b, flash):
    nx, ny = seg_normal(a, b)
    off = 2.5
    p1 = (a[0] + nx * off, a[1] + ny * off)
    p2 = (b[0] + nx * off, b[1] + ny * off)
    if flash:
        pt.line([(a[0] + nx * (off + 2.6), a[1] + ny * (off + 2.6)),
                 (b[0] + nx * (off + 2.6), b[1] + ny * (off + 2.6))],
                (150, 62, 20), 6.0)
    pt.line([p1, p2], (8, 10, 16), 4.6)
    pt.line([p1, p2], (255, 236, 190) if flash else COL_SLING_RUBBER, 3.3)
    pt.line([(p1[0] + nx * 1.0, p1[1] + ny * 1.0), (p2[0] + nx * 1.0, p2[1] + ny * 1.0)],
            (255, 255, 240) if flash else (255, 148, 118), 1.0)
    pt.line([a, b], (26, 34, 46), 2.2)
    pt.line([a, b], COL_RAIL_HI, 1.2)
    for p in (a, b):                 # 两端立柱
        pt.circle(p[0], p[1], 2.1, fill=COL_RAIL_LO)
        pt.circle(p[0], p[1], 1.5, fill=COL_RAIL_MID)
        pt.circle(p[0] - 0.4, p[1] - 0.5, 0.7, fill=COL_RAIL_HI)


def paint_black_hole(g, img):
    """黑洞 a_kout3:画在两挡板之间的落球口里(必须在护板之后,否则被 apron 盖掉)。"""
    pt = Painter(img)
    hx, hy = g["hole"]
    for r, col in ((13.5, (10, 16, 30)), (11.5, (5, 8, 16)), (9.0, (1, 2, 5))):
        pt.circle(hx, hy, r, fill=col)
    pt.arc(hx - 13.5, hy - 13.5, hx + 13.5, hy + 13.5, 200, 340, (96, 156, 235), 1.5)
    pt.arc(hx - 13.5, hy - 13.5, hx + 13.5, hy + 13.5, 20, 160, (52, 92, 165), 1.2)
    pt.circle(hx, hy, 8.6, outline=(24, 30, 44), w=0.8)


def paint_side_holes(g, img):
    """引力井 a_kout1 + hyperspace 洞 a_kout2:窄通道里的小尺寸深洞。

    这两个洞落在台面暗角,不加亮环就看不清"那里有东西"(差距清单 C5)。
    """
    pt = Painter(img)
    for key, halo in (("well", (70, 120, 200)), ("hs_hole", (200, 150, 70))):
        hx, hy = g[key]
        pt.circle(hx, hy, 8.6, fill=mix((14, 20, 34), halo, 0.10))
        pt.circle(hx, hy, 8.6, outline=mix(halo, (255, 255, 255), 0.25), w=0.9)
        for r, col in ((6.5, (9, 14, 26)), (5.5, (4, 6, 13)), (4.2, (1, 2, 5))):
            pt.circle(hx, hy, r, fill=col)
        pt.arc(hx - 6.5, hy - 6.5, hx + 6.5, hy + 6.5, 200, 340, halo, 1.1)


def paint_ring_lamps(g, img):
    """outer_circle 灯座:沿徽章外弧的暗灯(点亮态由渲染层的圆点叠加)。
    角度表 150/120/90/60/30 必须与 pb_render.c 的求值一致。"""
    pt = Painter(img)
    cx, cy, rad, n = g["ring"]
    for i in range(n):
        a = math.radians(150.0 - 30.0 * i)
        lx = cx + rad * math.cos(a)
        ly = cy - rad * math.sin(a)
        pt.circle(lx, ly, 3.4, fill=(7, 10, 18))
        pt.circle(lx, ly, 2.6, fill=(36, 44, 60))
        pt.circle(lx - 0.7, ly - 0.8, 0.8, fill=(70, 84, 104))


def paint_upgrade_lamps(g, img):
    """bmpr_inc_lights 升级灯座:中央 bumper 裙下方三盏(点亮态由渲染层叠加)。"""
    pt = Painter(img)
    # UI 实机反馈:三座仅隔 1.2px 且灰白描边,点亮态叠上去后观感是"歪扭的白条";
    # 缩小灯座、拉间距、描边改暗色,灭态就是一个安静的暗底座。
    cx, cy, dx, n = g["upg"]
    for i in range(n):
        lx = cx + (i - (n - 1) / 2.0) * dx
        pt.ell(lx - 4.2, cy - 3.4, lx + 4.2, cy + 3.4, fill=(6, 10, 18))
        pt.ell(lx - 3.6, cy - 2.9, lx + 3.6, cy + 2.9, outline=(40, 50, 66), w=0.8)
        pt.ell(lx - 3.0, cy - 2.4, lx + 3.0, cy + 2.4, fill=COL_LENS_OFF)


def paint_info_strip(g, img):
    """info_text_box 信息带:台面唯一的提示文字区,深色凹槽塑料件。

    渲染层的 lbl_msg 以不透明文字落在这条带子里,从此不会再压徽章弧字。
    """
    x0, y0, x1, y1 = g["info"]
    pt = Painter(img)
    pt.poly([(x0 - 2.2, y0 - 1.6), (x1 - 1.0, y0 - 2.6), (x1 + 2.2, y1 + 1.6),
             (x0 + 1.0, y1 + 2.6)], (7, 10, 18))
    pt.rect(x0, y0, x1, y1, fill=(3, 5, 10))
    pt.rect(x0, y0, x1, y1, outline=(30, 40, 56), w=0.9)
    pt.line([(x0 + 1, y1 - 0.6), (x1 - 1, y1 - 0.6)], (16, 22, 34), 0.8)
    pt.rect(x0 + 1.2, y0 + 1.2, x1 - 1.2, y0 + 2.6, fill=(1, 2, 5))


def draw_hole_halo(pt, strong):
    """虫洞光环精灵:常态低调呼吸,吸入/闪光时亮起(洞芯颜色与背景一致)。"""
    pt.circle(0, 0, 8.8, fill=(1, 2, 5))
    if strong:
        pt.arc(-14, -14, 14, 14, 190, 350, (150, 205, 255), 2.2)
        pt.arc(-14, -14, 14, 14, 10, 170, (90, 140, 220), 1.6)
        pt.circle(0, 0, 4.6, fill=(120, 185, 250))
        pt.circle(0, 0, 2.4, fill=(235, 248, 255))
    else:
        pt.arc(-14, -14, 14, 14, 200, 340, (80, 130, 205), 1.6)
        pt.arc(-14, -14, 14, 14, 20, 160, (45, 80, 140), 1.1)


def make_shadow():
    """球影:黑色椭圆 alpha 渐变,渲染层贴在球下偏移处制造立体感。"""
    w, h = 14, 10
    a = Image.new("L", (w * SS, h * SS), 0)
    ImageDraw.Draw(a).ellipse([2 * SS, 1.6 * SS, (w - 2) * SS, (h - 1.6) * SS],
                              fill=210)
    return dict(w=w, h=h, ox=-w // 2, oy=-h // 2,
                rgb=Image.new("RGB", (w, h), (0, 0, 0)),
                a=a.resize((w, h), Image.LANCZOS))


def render_sprites(g):
    """生成全部精灵,返回 (sprites, meta)。"""
    sp = {}

    r = g["ball_r"]
    pad = r + 3.2
    sp["ball"] = [make_sprite(lambda pt: draw_ball(pt, r), -pad, -pad, pad, pad)]

    L = g["flip_len"]
    flip = []
    for f in sorted(g["flippers"], key=lambda d: d["idx"]):
        frames = []
        for i in range(FLIP_FRAMES):
            t = i / (FLIP_FRAMES - 1)
            th = f["rest"] + (f["raised"] - f["rest"]) * t
            x0, y0, x1, y1 = flipper_bbox(th, L)
            frames.append(make_sprite(lambda pt, th=th: draw_flipper(pt, th, L),
                                      x0, y0, x1, y1))
        flip.append(frames)
    sp["flip"] = flip

    bump = []
    for c in g["circles"]:
        cx, cy, cr = c["c"][0], c["c"][1], c["r"]
        rad = cr * 0.60 + 3.6
        bump.append([
            make_sprite(lambda pt, cx=cx, cy=cy, cr=cr: draw_bumper_cap(pt, cx, cy, cr, False),
                        cx - rad, cy - rad, cx + rad, cy + rad),
            make_sprite(lambda pt, cx=cx, cy=cy, cr=cr: draw_bumper_cap(pt, cx, cy, cr, True),
                        cx - rad, cy - rad, cx + rad, cy + rad),
        ])
    sp["bump"] = bump

    lx0, ly = g["lanes"][0], g["lane_y"]
    sp["lane"] = [
        make_sprite(lambda pt: draw_lens(pt, lx0, ly, False),
                    lx0 - 8.6, ly - 6.6, lx0 + 8.6, ly + 6.6),
        make_sprite(lambda pt: draw_lens(pt, lx0, ly, True),
                    lx0 - 8.6, ly - 6.6, lx0 + 8.6, ly + 6.6),
    ]

    t0 = g["targets"][0]
    tx0, ty0 = (t0["a"][0] + t0["b"][0]) / 2.0, (t0["a"][1] + t0["b"][1]) / 2.0
    sp["tgt"] = [
        make_sprite(lambda pt: draw_target(pt, t0["a"], t0["b"], True),
                    tx0 - 12, ty0 - 9, tx0 + 12, ty0 + 9),
        make_sprite(lambda pt: draw_target(pt, t0["a"], t0["b"], False),
                    tx0 - 12, ty0 - 9, tx0 + 12, ty0 + 9),
    ]

    sling = []
    for s in g["slings"]:
        a, b = s["a"], s["b"]
        nx, ny = seg_normal(a, b)
        pts = [a, b,
               (a[0] + nx * 6.0, a[1] + ny * 6.0),
               (b[0] + nx * 6.0, b[1] + ny * 6.0),
               (a[0] - nx * 6.0, a[1] - ny * 6.0),
               (b[0] - nx * 6.0, b[1] - ny * 6.0)]
        x0 = min(p[0] for p in pts) - 1.0
        y0 = min(p[1] for p in pts) - 1.0
        x1 = max(p[0] for p in pts) + 1.0
        y1 = max(p[1] for p in pts) + 1.0
        sling.append([
            make_sprite(lambda pt, a=a, b=b: draw_sling(pt, a, b, False), x0, y0, x1, y1),
            make_sprite(lambda pt, a=a, b=b: draw_sling(pt, a, b, True), x0, y0, x1, y1),
        ])
    sp["sling"] = sling

    # 虫洞光环(中心即洞心,渲染层用 pb_hole_pos 直接放)与球影。
    sp["hole"] = [
        make_sprite(lambda pt: draw_hole_halo(pt, False), -15, -15, 15, 15),
        make_sprite(lambda pt: draw_hole_halo(pt, True), -15, -15, 15, 15),
    ]
    sp["shadow"] = [make_shadow()]

    # 共享精灵的逐实例位置:同形状、同角度,直接按几何中心平移。
    meta = dict(
        flip_frames=FLIP_FRAMES,
        lane_pos=[(sp["lane"][0]["ox"] + (lx - lx0), sp["lane"][0]["oy"])
                  for lx in g["lanes"]],
        tgt_pos=[(sp["tgt"][0]["ox"] +
                  ((s["a"][0] + s["b"][0]) / 2.0 - tx0),
                  sp["tgt"][0]["oy"] +
                  ((s["a"][1] + s["b"][1]) / 2.0 - ty0))
                 for s in g["targets"]],
        hole_pos=(sp["hole"][0]["ox"] + g["hole"][0],
                  sp["hole"][0]["oy"] + g["hole"][1]),
    )
    return sp, meta
