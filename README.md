# ai-passport-pinball

基于 [FoloToy AI Passport](https://github.com/FoloToy/ai-passport)（ESP32-C3 + ST7789P3 240x320 彩屏）的弹球游戏固件，玩法与台面结构对标 Windows 经典 **3D Pinball / Space Cadet**。

![status](https://img.shields.io/badge/target-ESP32--C3-blue) ![idf](https://img.shields.io/badge/ESP--IDF-5.5.3-orange)

## 玩法

规则、分值与台面分区逐条对标原版 Space Cadet：规格见 [`docs/space-cadet-spec.md`](docs/space-cadet-spec.md)，
实现对照状态见 [`docs/spec-compliance.md`](docs/spec-compliance.md)。下文的 §n 为规格条目号。

| 得分点 | base 分值 | 规格出处 |
| --- | --- | --- |
| 攻击 bumper ×3（中上） | 500 / 1000 / 1500 / 2000，按升级档位取 | §2.1 `control_bump_scores1[BmpIndex]` |
| 回弹立柱 ×4（环区） | 500 | §2.1 `control_rebo_score1[0]` |
| 弹弓 ×2（挡板上方两侧） | 500 | §2.1 `rebo3/4` |
| 顶部再入车道 ×3 | 2000；三盏全亮额外 +5000 后清空 | §2.2 `roll1/2/3` + D9 |
| 倍率目标 ×3（左侧） | 单个 500；三个全倒 1500 并倍率升档 | §2.3 `target7/8/9` |
| 星柱 ×3（右道） | 单个 500；三个全亮 2500 后清空 | D3 右道星柱 |
| 黑洞（两挡板之间的落球口） | 20000，吐球后冷却 6.0s | §2.4 `a_kout3` |
| 引力井（左上窄道） | 50000，冷却 8.0s；向上踢回顶拱 | §2.4 `a_kout1` + D10 |
| 超空间洞（右上发射道旁） | 10000 / 20000 / 50000 / 150000，按灯环档位取，满 4 清环，冷却 3.0s；向上踢回顶拱 | §2.4 `a_kout2` + D11 |

- **倍率**：实际入账 = base × 倍率，档位 x1/x2/x3/x5/x10，由倍率目标组完成推进，**每颗球结束归 x1**（§4.2）。
- **bumper 升级**：挥挡板点亮中央三盏升级灯，满 3 盏后穿过任意顶部车道即升档 +1（RC 5 "Weapons Upgraded"），满灯瞬间信息带提示 "UPG READY"（D12）。
- **顶部车道组**：穿过即亮（不再切换灭），三盏全亮 = +5000 车道组加成后清空（D9）。
- **星柱**：右道三颗四角星形柱，撞亮一枚 +500，三枚全亮 +2500 重置循环。
- **军衔晋升**：倍率目标组完成推进进度环 1 段，满 5 段晋升 1 级，共 9 级 CDT→FADM；晋升**不计分**（§3 `AddRankProgress`）。
- **球数与球保存**：3 颗球（§4.1）；每次发射瞬间武装一次球保存，5.0s 内掉落自动救回且球数不减（§4.3）。
- **最高分**：5 槽榜单 + 插入下移 + 校验和，NVS 持久化；标题页与结算页显示榜单，本局入榜行标 `*`（§5）。
- 记分板为 8 位宽，分数封顶 99999999（显示位宽限制，登记为规格 §6.1 偏差 D5）。
- 不移植的元素（任务链、multiball、多玩家、tilt、螺旋滑道、虫洞 sink 等）见规格 §6，代码中不出现其变体。

## 操作（三按键）

| 按键 | 标题页 | 结算页 | 发球道 | 台面 | 暂停菜单 |
| --- | --- | --- | --- | --- | --- |
| 上键 | 开始游戏 | 回标题 | — | 左挡板 | 上一项 |
| 下键 | 开始游戏 | 回标题 | — | 右挡板 | 下一项 |
| 确定键 | 开始游戏 | 回标题 | 按住蓄力、松开发射 | — | 确认当前选项 |
| 确定键长按 | — | — | 呼出暂停菜单 | 呼出暂停菜单 | — |

暂停菜单三项：RESUME（继续）/ RESTART（重开）/ EXIT（回标题）；后两项都会先结算本局分数并写入最高分榜单。

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
- **台面布局与规则**：对标 Microsoft 3D Pinball *Space Cadet*，台面分区、得分点分值、灯光推进与最高分
  规则均从 `k4zmu2a` 逆向源码逐条提取为书面规格后实现；数值取自源码分值表，坐标按 240x320 竖屏重新布局。
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
docs/           规格表、差距清单、规格对照表
tests/          主机单元测试(纯 C,无 IDF 依赖;含台面布局不变量)
```

## 模板契约

沿用 ai-passport 模板的强制约定：3MB 应用分区上限、`cardid@0x356000`、永久 Recovery `@0x700000`、开机长按上键 5 秒进 Recovery 的 bootloader 钩子（`bootloader_components/`），二创请勿改动分区表。
