# ai-passport-pinball

基于 [FoloToy AI Passport](https://github.com/FoloToy/ai-passport)（ESP32-C3 + ST7789P3 240x320 彩屏）的弹球游戏固件，玩法与台面结构对标 Windows 经典 **3D Pinball / Space Cadet**。

![status](https://img.shields.io/badge/target-ESP32--C3-blue) ![idf](https://img.shields.io/badge/ESP--IDF-5.5.3-orange)

## 玩法

- 3 颗球，掉落即失；发球后 8 秒内掉落自动救回（Ball Save）。
- 计分：pop bumper 1000、弹弓 250、掉落目标 500、顶部车道 100。
- 顶部三条车道全部点亮 → 得分倍率 +1（上限 x5），并奖励 2500 x 倍率。
- 左侧三个掉落目标全部放倒 → 奖励 5000 x 倍率，随后整组复位。
- 最高分保存在 NVS，掉电不丢。

## 操作（三按键）

| 按键 | 标题/结算页 | 发球道 | 台面 |
| --- | --- | --- | --- |
| 上键 | 开始游戏 | — | 左挡板 |
| 下键 | 开始游戏 | — | 右挡板 |
| 确定键 | 开始游戏 | 按住蓄力、松开发射 | — |
| 确定键长按 | — | 退回标题 | 退回标题 |

## 构建

需要 ESP-IDF 5.5.3（目标 `esp32c3`）：

```bash
idf.py set-target esp32c3
idf.py build
idf.py -p <PORT> flash monitor
```

主机单元测试（无需 ESP-IDF，任意 C 编译器）：

```bash
./tools/host_test.sh    # 物理引擎与台面几何的断言测试
```

台面美术为构建前烘焙（需要 Python 3 + Pillow）：

```bash
python tools/gen_assets.py --preview   # 重新生成 main/assets/pb_art.bin + pb_assets.c/.h,
                                       # 并导出 build/art/*.png 预览图供肉眼校对
```

生成器直接解析 `main/pb_table.c` 里的碰撞几何作画，所以美术与物理永远像素级对齐；改台面坐标后重跑一次即可。

## 素材与来源说明

- **台面视觉**：由 `tools/pb_art.py` + `tools/gen_assets.py` 程序化手绘（PIL 超采样绘制 → 烘焙成一张 240x320 RGB565 背景 + 一组 RGB565A8 精灵），风格对标 Space Cadet 的深空底 / 铬导轨 / 黄挡板 / 红 bumper，**未复制任何原版二进制素材**。
- **音效**：固件内合成的方波/扫频短音，非原版采样。
- **台面布局与规则**：结构上致敬 Microsoft 3D Pinball *Space Cadet*（顶部车道、三 bumper、掉落目标组、弹弓、发球道的经典组合），物理规则为独立实现。
- 若后续引入原版提取的位图/音频素材：版权归 Microsoft/Cinematronix 所有，仅供个人学习研究，请勿随固件分发或商用。

## 致谢

- [FoloToy ai-passport](https://github.com/FoloToy/ai-passport) —— BSP 组件（显示/LVGL/按键/音频）、分区表与 Recovery 兼容契约均沿用该模板。
- [k4zmu2a/SpaceCadetPinball](https://github.com/k4zmu2a/SpaceCadetPinball) —— 原版规则与台面结构的逆向参考。
- [sanderdesnaijer/esp32-pinball](https://github.com/sanderdesnaijer/esp32-pinball)（MIT）—— 物理引擎子步长碰撞思路的参考。

## 代码结构

```
main/
  main.c        入口: BSP 初始化 + 60Hz lv_timer 驱动
  pb_physics.c  物理核心: 自适应子步长、圆/线段/旋转挡板碰撞、单向阀
  pb_table.c    台面几何: 墙/弹弓/目标/bumper/发球道数据(美术的唯一真相源)
  pb_game.c     规则状态机: 计分/倍率/球数/球保存/最高分(NVS)
  pb_render.c   LVGL 渲染: 背景位图 + 精灵换帧,静态台面不重绘
  pb_assets.c   生成物: lv_image_dsc_t 资产表,指向 pb_art.bin 内偏移
  pb_audio.c    合成音效: 独立任务 + 队列,不阻塞 UI
main/assets/
  pb_art.bin    生成物: 烘焙台面位图 + 精灵(EMBED_FILES 嵌入 .rodata,不占 RAM)
tools/
  pb_art.py     美术库: 调色板/点阵字体/分层绘制/精灵生成
  gen_assets.py 驱动: 解析几何 -> 出图 -> 打包 bin -> 生成 C 表 -> 导出预览
  host_test.sh  主机单元测试入口
components/bsp/ 模板 BSP(未改动)
tests/          主机单元测试(纯 C,无 IDF 依赖)
```

## 模板契约

沿用 ai-passport 模板的强制约定：3MB 应用分区上限、`cardid@0x356000`、永久 Recovery `@0x700000`、开机长按上键 5 秒进 Recovery 的 bootloader 钩子（`bootloader_components/`），二创请勿改动分区表。
