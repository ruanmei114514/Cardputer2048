# Cardputer2048 —— Cardputer ADV 简易 2048

240×135 深色棋盘；右侧面板从上到下是电量（右上角）、`SCORE`、`BEST`、按键提示、状态。移动时方块滑动过去，合并的两个方块撞到一起后弹一下，新方块从小放大出现；无声音。

按键：

| 键 | 作用 |
| --- | --- |
| `;` `,` `.` `/` | 上 / 左 / 下 / 右（键盘右下角印箭头那四个键，**直接按、不用 Fn**） |
| `DEL`（退格） | 撤销上一步（只保留一步） |
| `-` / `+` | 屏幕亮度减 / 加（每按一次 ±25，范围 20~255；`=` 也当加亮，不用按 Shift） |
| `R` / 回车 | 重开一局 |

## 目录结构

```
Cardputer2048/
├── Cardputer2048-cardputer-adv.bin     ← 刷写用（整包，从 0x0 刷）
├── Cardputer2048-cardputer-adv-DEBUG-press-G.bin  ← 测试版，别日常用
├── README.md
└── src/Cardputer2048/                  ← 源码（Arduino sketch 文件夹）
    ├── Cardputer2048.ino               游戏本体：显示、键盘、流程、电量
    ├── game2048.h                      2048 核心逻辑 + 棋盘几何/动画参数（纯 C++）
    ├── Cardputer2048-cardputer-adv.bin 同一份固件，和源码放在一起
    └── test/
        ├── test_2048.cpp               PC 上的逻辑自检
        ├── preview_2048.cpp            PC 上跑动画出预览帧
        └── make_preview.py             把预览帧拼成 GIF / 连拍图
```

`.bin` 是整包镜像（bootloader + 分区表 + 程序，4MB）。正式版和源码目录里那份是同一个文件，刷哪个都行。

带 `DEBUG` 的那个是测试版，和正式版只差一行：**按 `G` 直接把 gameOver 置真**，用来快速查看 GAME OVER 画面（不用真打到死局）。只是调试方便，正常玩别刷它。对应的源码改动：

```cpp
else if (kb.isKeyPressed('g') || kb.isKeyPressed('G')) { gameOver = true; redraw(); }
```

## 直接刷固件

```
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 1500000 write_flash 0x0 Cardputer2048-cardputer-adv.bin
```

或者在 M5Burner 里选「自定义固件」，选这个 bin、起始地址 `0x0`。刷完 esptool 会自动重启，没起来就拨一下机身电源开关。

## 自己编译

Arduino IDE 2：库管理器装 `M5Unified`、`M5GFX`、`M5Cardputer`，开发板选 **M5Stack → M5Cardputer**，打开 `src/Cardputer2048/Cardputer2048.ino` 上传。

Cardputer ADV 目前还没有单独的板子选项，用 M5Cardputer 就行：程序启动时由 M5Unified 自动识别 ADV（识别失败时 `fallback_board` 兜底强制成 ADV），屏幕和键盘都按 ADV 的引脚走。

## 电量与"充电中"

右上角是电量图标 + 百分比；判定为充电中时百分比变绿并显示 `CHG`。

- **充电状态是推断出来的，不是硬件信号。** ADV 没把充电状态接到 MCU：电路图上 TP4057 的 `CHRG`/`STDBY` 悬空，M5Unified 对 ADV 的 `isCharging()` 也返回 `charge_unknown`。
- 推断规则（`batteryTick()`）：每 5 秒采样 4 次取平均并平滑；5 秒内跳变 ≥25mV，或 30 秒净升 ≥15mV → 判为充电中；反向跳变则判为未充电。判定结果会保持，不会因为电压不动而掉回。
- 代价：插上后最慢 30 秒才显示 `CHG`；开机时如果电池已经充满（恒压阶段电压基本不动），可能一直判不出来，需要拔插一次。
- **插上 USB 时百分比会往上跳约 10%**，这是正常现象：电量是电池端电压线性换算（3300mV=0%、4150mV=100%，1% = 8.5mV），充电电流在电池内阻上把端电压抬高 80~90mV，就对应 10%，拔掉会落回来。官方 UserDemo / UiFlow2 用的同一个 `getBatteryLevel()`，同样会跳。
- 板上没有库仑计，所以显示的是"当前端电压对应的电量"，不是精确剩余容量。

## 存档

最高分 `BEST` 和当前局面都存在 nvs 分区里（20KB，`Preferences`，不需要额外依赖、不动分区表）。

- 开机时如果 nvs 里有一局**没打完**的存档，会在棋盘上弹一个选择框：`ENTER: continue` 继续上一局，`R: new game` 重开。
- 上一局已经 GAME OVER 的存档会被忽略，直接开新局（死局续玩没意义）。
- 落盘时机：挂在原有的 5 秒心跳里，只有局面变了才写一次。写入约 10~20ms，发生在动画之外，看不出来；代价是断电时最坏丢最后几秒的操作。
- 最高分一破纪录就立刻写。
- **注意：刷整包 bin 会把存档清掉。** 整包镜像从 0x0 覆盖到 0x400000，而 nvs 在 0x9000，镜像里那一段是 `0xFF`（擦除态）。想保留存档就只刷 app：

  ```
  esptool --chip esp32s3 --port /dev/ttyACM0 --baud 1500000 write_flash 0x10000 Cardputer2048.ino.bin
  ```

  存档能活过重启、关机、直接断电，但活不过刷整包固件。

## 本地看动画（不用上机）

```
cd src/Cardputer2048
g++ -std=c++11 -o /tmp/t2048 test/test_2048.cpp && /tmp/t2048          # 逻辑自检
g++ -std=c++11 -o /tmp/p2048 test/preview_2048.cpp && /tmp/p2048 /tmp/cp2048_preview
python3 test/make_preview.py /tmp/cp2048_preview                        # 出 preview.gif 和 move_strip.png
```

预览和固件共用 `game2048.h` 里的 `frameTiles()`：哪一帧哪个方块画在哪，是同一份算法，预览只换了光栅化和右侧面板文字。

## 说明

- 棋盘和右侧信息栏都画在内存画布上、一帧推一次屏，所以移动时不会闪。
- 刻意没做：分数存盘（断电清零）、音效。想要哪个说一声。
