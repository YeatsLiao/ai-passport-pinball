# 原版玩法-UI-得分规格表（Space Cadet）

本文是从 `SpaceCadetPinball-web`（k4zmu2a 逆向工程版）源码逐行提取的**书面规格**，
是本固件玩法、UI、分值的唯一对照基准。每条规格都给出源码出处，便于追溯。

- 分值表出处：`SpaceCadetPinball/control.cpp` L35-L58（数组定义）、L294-L384（`score_components[88]` 组件→控制函数→分值数组绑定）
- 倍率/球数出处：`SpaceCadetPinball/TPinballTable.cpp` L44、L262-L299、L82
- 虫洞出处：`SpaceCadetPinball/control.cpp` L557-L570、L1541-L1596
- Hyperspace 出处：`SpaceCadetPinball/control.cpp` L2208-L2259
- 黑洞/引力井出处：`SpaceCadetPinball/control.cpp` L1900-L1940
- 球保存出处：`SpaceCadetPinball/control.cpp` L2043-L2057、L2465-L2516
- 军衔/燃料出处：`SpaceCadetPinball/control.cpp` L535、L952-L987、L1419-L1519
- 最高分出处：`SpaceCadetPinball/high_score.cpp` L17-L122
- 文案出处：`SpaceCadetPinball/pinball.cpp` L7-L120（RC 字符串表）
- 布局出处：`SpaceCadetPinball/Screenshots/screenshot-1.jpg`、`screenshot-2.jpg`

## 1. 台面布局分区

原版台面为横向透视投影（600×416），本固件为 240×320 竖屏。移植时保留**分区拓扑与
元素归属**，不保留像素坐标。

| 分区 | 原版内容（自上而下） | 截图依据 |
| --- | --- | --- |
| A 顶部分流区 | 三条再入车道（`roll1/2/3` + 灯 `lite8/9/10`），车道分隔短柱；右侧发射滑道入口越过顶部弧 | 顶部三个黄色灯插 + 拱形导轨 |
| B 左上滑道区 | 紫色螺旋滑道（launch ramp，`ramp` 5000）、`oneway4` 单向、旗标旋转器 `a_flag1` | 左侧紫色螺旋 ramp |
| C 攻击 bumper 组 | `a_bump1..4` 四个 pop bumper，围成中央上方菱形 | 中上三个红白星形 bumper + 左上第 4 个 |
| D 虫洞区 | `sink1/2/3` 三个虫洞 sink + 出口指示灯 `lite4/5/6/7` + `worm_hole_lights` | 中上偏左的三个暗洞 |
| E 中央环形区 | `outer_circle`（燃料/军衔进度环）、`middle_circle`（9 级军衔环）、`fuel_bargraph`（12 段）、`goal_lights`、行星图案中心 | 台面正中的大圆环 + 蓝行星 |
| F 右上发射/超空间区 | 发射车道 `roll110/111/112`、`a_kout2` hyperspace kick-out、`hyper_lights` | 右上棕色弧形滑道 |
| G 左侧目标组 | `target1..3` 助推、`target4..6` 勋章、`target7..9` 倍率、`target10..12` 燃料、`target13..15` 任务、`target16..21` 危险、`target22` 虫洞目的 | 左中散布的橙色小目标 |
| H 中场弹弓区 | `rebo1..4` 回弹器（弹弓）、`a_kick1/2` kickback、`rebo3/4` | 挡板上方两个三角弹弓 |
| I 内/外轨道区 | `roll4/roll8` outlane（20000）、`roll6/roll7` return lane、`roll5` bonus lane、`roll179..184` 燃料车道（6 条） | 挡板两侧的竖条车道 |
| J 挡板区 | `a_flip1/a_flip2` 双挡板、底部护板 apron、`a_kout3` 黑洞（两挡板之间的落球口）、`drain` | 底部中央紫色星芒 + 暗洞 |
| K 右侧滑道区 | 逃生滑道（escape chute）、`sink7`、`gate2` | 右侧棕色长弧滑道 |
| L 记分板 | 独立于台面：logo 图、`BALL n`、分数、`Player n`、信息文本框（`info_text_box`）、任务文本框 | screenshot-2 右半区 |

**关键拓扑结论**：黑洞 `a_kout3` 位于**两挡板之间的落球口**（分区 J），是"球没接住才会
进"的踢出洞，不是台面中部的可路过点。原版落球顺序是：挡板间隙 → 黑洞（有分、吐出）或
`drain`（掉球）。

## 2. 得分点与分值（原版全量映射）

`AddScore(base)` 的实际入账为 `ScoreAdded + base * score_multipliers[ScoreMultiplier]`
（`TPinballTable.cpp` L276）。下表 base 均为**未乘倍率**的原始值。

### 2.1 碰撞类

| 组件 | 控制函数 | base 分值 | 取值依据 |
| --- | --- | --- | --- |
| `a_bump1..4` 攻击 bumper | `BumperControl` | 500 / 1000 / 1500 / 2000 | `control_bump_scores1[BmpIndex]`，BmpIndex=升级档位 0..3 |
| `a_bump5..7` 发射 bumper | `BumperControl` | 1500 / 2500 / 3500 / 4500 | `control_bump_scores2[BmpIndex]` |
| `rebo1/2` 挡板回弹器 | `FlipperRebounderControl1/2` | 500 | `control_rebo_score1[0]` |
| `rebo3/4` 弹弓回弹器 | `RebounderControl` | 500 | `control_rebo_score1[0]` |
| `ramp` 发射滑道 | `LaunchRampControl` | 5000 | `control_ramp_score1[0]` |
| `a_flag1/2` 旗标旋转器 | `FlagControl` | 500 / 2500 | `control_flag_score1[0/1]`（升级前后） |
| `oneway4` 滑道单向 | `DeploymentChuteToEscapeChuteOneWayControl` | 15000 / 30000 / 75000 / 30000 / 15000 / 7500 | `control_oneway4_score1[0..5]` |

### 2.2 车道 / rollover 类

| 组件 | 控制函数 | base 分值 |
| --- | --- | --- |
| `roll1/2/3` 再入车道（顶部三车道） | `ReentryLanesRolloverControl` | 2000 |
| `roll110/111/112` 发射车道 | `LaunchLanesRolloverControl` | 500 |
| `roll4/roll8` outlane（外轨道） | `OutLaneRolloverControl` | 20000 |
| `roll6/roll7` return lane（返回轨道） | `ReturnLaneRolloverControl` | 500 / 2500 |
| `roll5` bonus lane | `BonusLaneRolloverControl` | 10000 |
| `roll179..184` 燃料车道 ×6 | `FuelRolloverNControl` | 500 |
| `roll9` 空间扭曲 | `SpaceWarpRolloverControl` | 10000 |

### 2.3 目标（stand-up target）类

| 组件 | 控制函数 | base 分值 |
| --- | --- | --- |
| `target1/2/3` 助推目标 | `BoosterTargetControl` | 单个 500；三个全倒且灯组满足条件时 5000 |
| `target4/5/6` 勋章目标 | `MedalTargetControl` | 单个 1500；组完成递进 10000 / 50000 |
| `target7/8/9` 倍率目标 | `MultiplierTargetControl` | 单个 500；三个全倒 1500 + 倍率升档 |
| `target10/11/12` 燃料点目标 | `FuelSpotTargetControl` | 750 |
| `target13/14/15` 任务点目标 | `MissionSpotTargetControl` | 1000 |
| `target16..21` 危险点目标 | `Left/RightHazardSpotTargetControl` | 750 |
| `target22` 虫洞目的目标 | `WormHoleDestinationControl` | 750 |

### 2.4 洞（sink / kickout）类

| 组件 | 控制函数 | base 分值 | 触发与后续 |
| --- | --- | --- | --- |
| `sink1/2/3` 虫洞 | `WormHoleControl` | 2500（常态）/ 5000（目的灯匹配，额外 Replay）/ 7500（不匹配） | `get_scoring(0/1/2)`；同时点亮出口灯组并把球传送到下一 sink |
| `a_kout2` hyperspace | `HyperspaceKickOutControl` | 10000 / — / 20000 / 50000 / 150000 | 按 `hyper_lights` 已亮点数 0..4 取 `control_kickout_score1[0/2/3/4]`；第 3 档附赠 extra ball，第 4 档清环并触发引力井奖 |
| `a_kout3` 黑洞 | `BlackHoleKickoutControl` | **20000** | `control_kickout_score2[0]`；文案 RC 80 `"Black Hole\n%ld"`；随后 `Message(55,-1)` 进入 `TimerTime1` 冷却（`TKickout.cpp` L67-L74） |
| `a_kout1` 引力井 | `GravityWellKickoutControl` | **50000** | `control_kickout_score3[0]`；文案 RC 82 `"Gravity Normalized\n%ld"` |
| `sink7` 逃生滑道 sink | `EscapeChuteSinkControl` | — | 仅按 `TimerTime` 延时吐球 |
| `drain` 落球口 | `BallDrainControl` | — | 见 §4 |

## 3. 灯光与动画清单

| 组 | 组件 | 行为 |
| --- | --- | --- |
| 再入车道灯 | `lite8/9/10` | 车道 rollover **切换**（`ReentryLanesRolloverControl` L1210-L1258）：每盏先 `Message(19/20)` 翻转；**仅在该盏由灭变亮的那一拍**检查 `bmpr_inc_lights.Message(37)`（已亮数）是否等于 `Message(38)`（总数），满则 `Message(0)` 清空 + `attack_bump.Message(12)`（BmpIndex+1）+ RC 5 |
| bumper 升级灯组 | `bmpr_inc_lights`（`lite169/170/171`） | 挡板按下 → `LeftFlipperControl`/`RightFlipperControl`（L1598-L1614）对组发 `Message(24)`/`Message(25)`；`TLightGroup::Message` L72-L125 表明 24/25 是**保持亮数的循环移位**（不是递增），移位方向左右相反。满组条件与文案见上一行 |
| 发射 bumper 升级灯组 | `ramp_bmpr_inc_lights`（`lite172/173/174`） | 由**发射车道** `roll110/111/112` 的切换灯驱动（`LaunchLanesRolloverControl` L1273-L1314），满组 → `launch_bump` 升级，文案 RC 6 `"Engine Upgraded"` |
| 虫洞出口灯 | `lite4/5/6/7` + `worm_hole_lights` + `bsink_arrow_lights` | `lite4.MessageField` 记录当前目的 sink；命中时按 `wormhole_tag_array2/3` 闪 5.0s |
| 超空间灯组 | `hyperspace_lights` | 每命中 +1 段，0..4 段决定奖励档位；第 4 段满 → `Message(0)` 清空 |
| 倍率灯组 | `top_target_lights` | 组数 1/2/3/4 → `ScoreMultiplier` = 1/2/3/4，文案 RC 56-59 `"Field Multiplier 2x/3x/5x/10x"` |
| 燃料进度 | `fuel_bargraph`（12 段） | 燃料车道/燃料目标点亮，`Message(45, n)` 设定段数；已足段数时只闪不增（RC 44 `"Ship Re-Fueled"`） |
| 军衔外环 | `outer_circle` | `AddRankProgress(n)`（L952-L987）点亮 n 段；满环 → `middle_circle` +1 且外环清 5.0s 闪。**全程不调用 `AddScore`**，晋升只给 RC 83 文案 |
| 军衔内环 | `middle_circle` | 0..9 段，对应 9 级军衔（`RankRcArray[9]` = RC 84..92：Cadet/Ensign/Lieutenant/Captain/Lt Commander/Commander/Commodore/Admiral/Fleet Admiral）；晋升文案 RC 83 `"Promotion to %s"` |
| 球保存灯 | `lite200`（shoot again） | 发射后亮 5.0s；掉球时若亮 → 消耗并 RC 96 `"Re-Deploy"` |
| 额外球灯 | `lite199`（replay） | `table_set_replay` 置亮，RC 0 `"Replay Awarded"`；掉球时消耗 → RC 95 `"Replay Ball"` |
| 目标组灯 | `bumper_target_lights`、`bpr_solotgt_lights`、`goal_lights`、`lchute_tgt_lights`、`skill_shot_lights`、`l_trek_lights`/`r_trek_lights` | 任务/技能流程指示，随任务链推进 |
| 球体动画 | `TBall` + 阴影 | 球精灵 + 偏移阴影；bumper 帽 2 帧（常态/压下）、挡板多帧、灯芯 2 帧 |
| 文本框 | `info_text_box`（2.0s 自动消失）、`mission_text_box`（8.0s / -1 常驻） | 台面上的信息层，与记分板分离 |

## 4. 球数 / 倍率 / 球保存规则

1. **球数**：`MaxBallCount = 3`（`TPinballTable.cpp` L82）。显示值 `MaxBallCount - BallCount + 1`
   （L296），即第 1 颗球显示 "1"，第 3 颗显示 "3"。`BallCount <= 0` 时移除球数显示并结束局。
2. **倍率**：`score_multipliers[5] = {1, 2, 3, 5, 10}`（L44）。`ScoreMultiplier` 索引 0..4，
   由倍率目标组完成次数推进（`MultiplierTargetControl` L2430-L2448）；每球结束归 0（L576）；
   换玩家归 0（L432）。特殊分（`SpecialAddScore`）入账时**临时归零倍率**（L942-L943）。
3. **球保存（shoot again）**：仅在**发射瞬间**武装，`lite200` 亮 5.0s（`ShootAgainLightControl`
   L2043-L2057）。`MessageField` 交替，保证同一次发射只武装一次。掉球时若灯仍亮 →
   `Message(20)` 熄灯 + RC 96 提示 + 直接重新发球，**球数不减**（`BallDrainControl` L2501-L2507）。
4. **额外球（replay / extra ball）**：`table_set_replay` 点亮 `lite199`；掉球时若亮 → 熄灯、
   点亮 `lite200`、`ExtraBalls` +1（L2508-L2516）。`ExtraBalls` 在球数耗尽时抵扣（L2530）。
5. **tilt**：`nudge_count` 超阈值 → `TPinballTable::tilt()` 置 `TiltLockFlag = 1`，
   所有得分组件停止响应（各 `Collision` 里的 `if (!PinballTable->TiltLockFlag)`），
   显示 RC 35 `"TILT!"`，直到本球结束 `pb::tilt_no_more()`。
6. **分数上限**：`CurScore > 1e9` 时进位 `CurScoreE9` 并回绕（L278-L282），即无 7 位截断。
7. **无限球**：`table_unlimited_balls` 仅由调试/密钥开启（L2489），正常玩法不存在无限掉球保护。

## 5. 最高分记录规则（`high_score.cpp`）

1. **表结构**：固定 5 槽 `high_score_struct { char Name[32]; int Score; }`，槽位 0 分数最高。
2. **空表哨兵**：`clear_table` 写入 `Score = -999`、`Name[0] = 0`（L75-L83）。
3. **入榜判定**：`get_score_position`（L85-L96）
   - `score <= 0` → 返回 -1（0 分不入榜）；
   - 否则返回**第一个** `table[i].Score < score` 的 i；
   - 5 槽都不小于该分数 → 返回 -1（未上榜）。
4. **插入与下移**：`place_new_score_into`（L98-L122）把 `table[position..4]` 整体向下滑一格
   （从槽 4 反向 memcpy），再在 `position` 写入新分数与名字；名字长度截断到 31 字符。
5. **持久化**：`write`（L48-L73）按 `"{i}.Name"` / `"{i}.Score"` 逐槽落盘，并写
   `"Verification"` = Σ(各槽 Name 所有字符) + Σ(各槽 Score)。`read`（L17-L46）读回后重算校验和，
   **不匹配则整表清零**（L42-L44），默认校验值 7。
6. **交互**：命中榜单时弹 "High Scores" 对话框，在对应槽位内联编辑名字，Ok 才落盘、
   Cancel 放弃（L142-L232）；另有 Clear 二次确认清空全表。

## 6. 移植范围（本固件 240×320 / 单玩家 / 三键 / 1MB 分区）

以下原版元素**明确不在本轮范围**，实现中不得出现其变体，也不得用自创规则顶替：

| 原版元素 | 不移植原因 |
| --- | --- |
| 任务链（17 个 mission、`MissionControl`、`mission_select_scores`） | 需要任务文本框 + 多阶段状态机，UI 面积与分区预算不足 |
| Multiball / 球锁定（`MultiballFlag`、`table_bump_ball_sink_lock`） | 物理引擎为单球实现 |
| 三玩家轮替、`PlayerCount` | 设备无玩家选择 UI |
| Tilt / nudge | 设备无加速度计 |
| 左侧紫色螺旋滑道、右侧逃生滑道（`ramp`/`oneway4`/`sink7`） | 需要多段轨道与坡道渲染，超出 240px 宽度 |
| 6 条燃料车道 `roll179..184`、`fuel_bargraph` 12 段 | 挡板两侧无车道空间 |
| 勋章/任务/危险/虫洞目的目标组（`target4..22`） | 台面目标位不足 |
| kickback（`a_kick1/2`）、bonus lane、space warp rollover | 需额外轨道 |
| 名字输入对话框 | 三键设备无字符输入 |

以下元素**在本轮范围内**，且必须与 §2/§3/§4/§5 的原版数值/规则逐一对应：

| 原版元素 | 本固件载体 |
| --- | --- |
| 攻击 bumper ×4（500/1000/1500/2000 档位） | 3 个 pop bumper（`circles[i].kick > 0`）+ `bump_tier` 0..3 |
| 回弹立柱 `rebo3/4` 以外的 kick==0 小立柱 | 固定 500（§2.1 `control_rebo_score1[0]`），不走 bumper 档位分 |
| bumper 升级灯组（`bmpr_inc_lights`，3 盏） | `bump_prog` 0..3（已亮盏数）+ 三盏升级灯 |
| 再入车道 `roll1/2/3`（2000，切换式点亮） | 顶部三车道 `lane_lit[]` |
| 倍率目标组 `target7/8/9`（500/1500 + 升档） | 左侧三掉落目标 `target_down[]` |
| `score_multipliers {1,2,3,5,10}` | `mult_idx` 0..4 |
| 回弹器 `rebo3/4`（500） | 两侧弹弓 |
| `a_kout3` 黑洞（20000 + 冷却） | 两挡板尖端之间落球口的踢出洞 |
| `a_kout1` 引力井（50000 + 冷却） | 左上窄通道洞 |
| `a_kout2` hyperspace（10000/20000/50000/150000） | 右上发射道旁洞 + `hs_lights` 0..3 档位（满 4 清环） |
| `outer_circle` 满环 → `middle_circle` 军衔晋升 | 军衔进度环 5 段 `ring_lit` + 9 级军衔名 `rank` |
| 球保存 5.0s、每球每次发射武装一次 | `ball_save` / `ball_save_used` |
| RC 文案 5/12/49/80/81/82/83/96 | `show_msg` 台面信息带提示 |
| 5 槽最高分 + 插入下移 + 校验和 | NVS `hst` blob + 标题页/结算页 5 行榜单 |

### 6.1 移植偏差（已在代码里固定登记，不得静默修改）

以下条目是“在范围内但无法逐字照搬”的部分，每条都给出原版依据与偏离理由：

| # | 条目 | 原版 | 本固件 | 理由 |
| --- | --- | --- | --- | --- |
| D1 | 三个 kickout 洞的冷却时长 | `TKickout::Message(55,-1)` 读 `.dat` 的 `TimerTime1`（未随源码发布） | 黑洞 6.0s / 引力井 8.0s / hyperspace 3.0s | 数值不可得，取手感值；必须保留“冷却”这一行为本身 |
| D2 | `bmpr_inc_lights` 推进方式 | `Message(24/25)` 循环移位（保持亮数） | 递增计数 `bump_prog++` | 原版移位从“全灭”永不会点亮，初态需 `.dat` 预设图案；递增保留“挥挡板 3 次 → 升级”的可玩链，不新增玩法 |
| D3 | 起始军衔 | 新局从 `middle_circle` 0 段 = Cadet | `rank = 1`（CDT） | 一致，仅登记以免误读为“从 0 开始” |
| D4 | 两个文本框 | `info_text_box`（2.0s）+ `mission_text_box`（8.0s/-1） | 合并为一条信息带（任务链不在范围 §6） | 240×320 只容得下一条横向空档 |
| D5 | 分数上限 | `CurScore > 1e9` 进位 `CurScoreE9` 并回绕 | 封顶 99999999 | 记分板只有 8 位宽；不模拟进位段 |
| D6 | 榜单校验和 | Σ(名字字符) + Σ(分数) | Σ(分数) + 固定盐值 | 三键设备无名字输入（§6 已列为不移植），无字符可参校 |
| D7 | 黑洞踢出落点 | `.dat` 中的 `a_kout3` 出口坐标（未发布） | 在洞上方 (107, 296) 以 vy=-540 向上吐回 | 必须落在挡板之间空档内，不能把球塞进墙里 |
| D8 | 异常恢复 | 无（原版靠 `unlimited_balls` 调试钩子） | 球低速滞留 1.2s 救球 / 球不活跃且三洞过场空闲 2.2s 按掉球处理 | 防止"球消失且球数不推进"的假死；不计分、不改玩法 |
| D9 | 顶部车道灯组规则 | `ReentryLanesRolloverControl` 切换式（亮→灭/灭→亮），仅灭→亮时检查 bumper 升级组 | 穿过即亮（不再切换灭），三盏全亮 = 车道组加成 5000 后清空 | 原版切换式在升档条件简化后灯态不再参与任何规则，灯组失去成组意义；补经典弹球成组玩法（实机反馈连续两轮问"顶部三个灯是干嘛的"） |
| D10 | 引力井踢出方向 | `.dat` 中的 `a_kout1` 出口坐标（未发布） | 在洞下方 (21, 76) 以 (-12, -420) 向上踢，沿左上窄通道回顶拱 | 原向下吐回左导轨，捕获感像白吞一球；向上踢符合"捕获-弹射"直觉，窄通道净宽 16 > 球径 8，出口接顶拱缓坡保证慢球滚离 |
| D11 | hyperspace 踢出方向 | `.dat` 中的 `a_kout2` 出口坐标（未发布） | 在洞下方 (196, 76) 以 (-50, -400) 向上踢，沿右上窄通道回顶拱 | 同 D10，原向下吐回右道；向上踢统一两个侧洞行为 |
| D12 | 车道升档条件 | 再入车道某盏由灭变亮时检查 `bmpr_inc_lights` 满组 → 清组 + BmpIndex+1 | `bump_prog` 满后穿过任意车道即升档（不再依赖车道灯 toggle） | 原 toggle 判定：车道灯是切换式，满灯后穿过已亮车道=灭灯错过升档，触发窗口极窄（实机反馈"过了好多下都没触发"）；简化后升档频率合理且直观 |
