// 2048 核心逻辑 + 棋盘几何/动画参数：纯 C++，不依赖 Arduino。
// test/test_2048.cpp 在 PC 上做逻辑自检，test/preview_2048.cpp 用同一套参数出预览帧。
#pragma once

#include <cstdint>
#include <cstring>

namespace g2048 {

enum { N = 4, UP, DOWN, LEFT, RIGHT };  // 方向从 1 开始
typedef uint16_t Tiles[N][N];

// ---------- 屏幕布局（Cardputer ADV 横屏 240x135）----------
constexpr int TILE    = 28;
constexpr int GAP     = 4;
constexpr int BOARD_X = 5;
constexpr int BOARD_Y = 5;
constexpr int BOARD_W = N * TILE + (N - 1) * GAP;  // 124

inline int cellX(int x)
{
    return BOARD_X + x * (TILE + GAP);
}
inline int cellY(int y)
{
    return BOARD_Y + y * (TILE + GAP);
}

// ---------- 方块配色（RGB888），下标 = log2(值) - 1 ----------
constexpr uint32_t TILE_RGB[11] = {0xeee4da, 0xede0c8, 0xf2b179, 0xf59563, 0xf67c5f, 0xf65e3b,
                                   0xedcf72, 0xedcc61, 0xedc850, 0xedc53f, 0xedc22e};

inline int tileColorIndex(uint16_t v)
{
    int e = 0;
    for (uint16_t t = v; t > 2; t >>= 1) ++e;  // v=2 -> 0, v=4 -> 1 ...
    return e;
}
inline uint32_t tileRgb(uint16_t v)
{
    int e = tileColorIndex(v);
    return e < (int)(sizeof(TILE_RGB) / sizeof(TILE_RGB[0])) ? TILE_RGB[e] : 0x3c3a32;
}

// ---------- 动画时序：滑动 SLIDE_STEPS 帧，之后合并/生成 POP_STEPS 帧 ----------
constexpr int   SLIDE_STEPS    = 4;
constexpr int   POP_STEPS      = 3;
constexpr int   FRAME_MS       = 14;
constexpr float POP_SCALE[POP_STEPS]   = {1.20f, 1.08f, 1.0f};
constexpr float SPAWN_SCALE[POP_STEPS] = {0.40f, 0.75f, 1.0f};

inline void clearBoard(Tiles b)
{
    memset(b, 0, sizeof(Tiles));
}

inline int emptyCount(const Tiles b)
{
    int n = 0;
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
            if (!b[y][x]) ++n;
    return n;
}

// 把第 i 条线（行/列）的第 j 个格子换算成棋盘坐标，j 的方向就是移动方向。
inline void cellFor(int dir, int i, int j, int &x, int &y)
{
    switch (dir) {
        case LEFT:  x = j;     y = i;     break;
        case RIGHT: x = N - 1 - j; y = i; break;
        case UP:    x = i;     y = j;     break;
        default:    x = i;     y = N - 1 - j; break;  // DOWN
    }
}

// 每个格子的方块是从哪一格滑过来的（-1 = 该格移动后是空的）；
// merged 的格子是两个方块撞过来的，from2 记第二个来源。
struct MoveInfo {
    int8_t fromX[N][N];
    int8_t fromY[N][N];
    int8_t from2X[N][N];
    int8_t from2Y[N][N];
    bool   merged[N][N];

    void reset()
    {
        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                fromX[y][x]  = -1;
                fromY[y][x]  = -1;
                from2X[y][x] = -1;
                from2Y[y][x] = -1;
                merged[y][x] = false;
            }
        }
    }
};

// 朝 dir 移动一次；返回棋盘是否变化，scoreGained 返回本次合并得分。
inline bool move(Tiles b, int dir, uint32_t &scoreGained, MoveInfo *info = nullptr)
{
    bool changed = false;
    uint32_t gained = 0;
    if (info) info->reset();

    for (int i = 0; i < N; ++i) {
        uint16_t line[N] = {0}, out[N] = {0};
        int  src[N]       = {0};
        int  src2[N]      = {0};
        bool didMerge[N]  = {false};
        for (int j = 0; j < N; ++j) {
            int x, y;
            cellFor(dir, i, j, x, y);
            line[j] = b[y][x];
        }

        // 压缩 + 合并，每个格子每次移动最多参与一次合并
        int k = 0;
        bool justMerged = false;
        for (int j = 0; j < N; ++j) {
            if (!line[j]) continue;
            if (k > 0 && out[k - 1] == line[j] && !justMerged) {
                src2[k - 1] = j;
                out[k - 1] = (uint16_t)(line[j] * 2);
                gained += out[k - 1];
                didMerge[k - 1] = true;
                justMerged = true;
            } else {
                out[k]      = line[j];
                src[k]      = j;
                src2[k]     = -1;
                didMerge[k] = false;
                ++k;
                justMerged = false;
            }
        }

        for (int j = 0; j < N; ++j) {
            int x, y;
            cellFor(dir, i, j, x, y);
            if (b[y][x] != out[j]) changed = true;
            b[y][x] = out[j];
            if (info) {
                if (j < k) {
                    int sx, sy;
                    cellFor(dir, i, src[j], sx, sy);
                    info->fromX[y][x]  = (int8_t)sx;
                    info->fromY[y][x]  = (int8_t)sy;
                    info->merged[y][x] = didMerge[j];
                    if (didMerge[j]) {
                        int s2x, s2y;
                        cellFor(dir, i, src2[j], s2x, s2y);
                        info->from2X[y][x] = (int8_t)s2x;
                        info->from2Y[y][x] = (int8_t)s2y;
                    } else {
                        info->from2X[y][x] = -1;
                        info->from2Y[y][x] = -1;
                    }
                } else {
                    info->fromX[y][x]  = -1;
                    info->fromY[y][x]  = -1;
                    info->from2X[y][x] = -1;
                    info->from2Y[y][x] = -1;
                    info->merged[y][x] = false;
                }
            }
        }
    }

    scoreGained = gained;
    return changed;
}

inline bool canMove(const Tiles b)
{
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            if (!b[y][x]) return true;
            if (x + 1 < N && b[y][x] == b[y][x + 1]) return true;
            if (y + 1 < N && b[y][x] == b[y + 1][x]) return true;
        }
    }
    return false;
}

inline uint16_t maxTile(const Tiles b)
{
    uint16_t m = 0;
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
            if (b[y][x] > m) m = b[y][x];
    return m;
}

// ---------- 一帧要画的方块（像素坐标 + 边长 + 数值），固件和 PC 预览共用 ----------
struct DrawCmd {
    int16_t  x, y, size;
    uint16_t value;
};

// t=0 刚按下、t=1 到位；mergeScale/spawnScale 是落地后几帧的放大系数。
// 空位不入列，由渲染端自己铺棋盘底色。
inline int frameTiles(const Tiles b, const MoveInfo *info, float t, int spawnX, int spawnY,
                      float mergeScale, float spawnScale, DrawCmd *out, int maxOut)
{
    int n = 0;
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            uint16_t v = b[y][x];
            if (!v) continue;
            int dx = cellX(x), dy = cellY(y);

            if (x == spawnX && y == spawnY) {  // 新生成的方块从小放大出现
                int sz = (int)(TILE * spawnScale);
                if (sz > 4 && n < maxOut)
                    out[n++] = DrawCmd{(int16_t)(dx + (TILE - sz) / 2), (int16_t)(dy + (TILE - sz) / 2),
                                       (int16_t)sz, v};
                continue;
            }

            if (info && t < 1.0f && info->merged[y][x]) {
                // 两个方块撞进同一格：滑动阶段画原来那两个，落地后才画合成值
                const int8_t fx[2] = {info->fromX[y][x], info->from2X[y][x]};
                const int8_t fy[2] = {info->fromY[y][x], info->from2Y[y][x]};
                for (int i = 0; i < 2; ++i) {
                    if (fx[i] < 0 || n >= maxOut) continue;
                    int px = dx + (int)((cellX(fx[i]) - dx) * (1.0f - t));
                    int py = dy + (int)((cellY(fy[i]) - dy) * (1.0f - t));
                    out[n++] = DrawCmd{(int16_t)px, (int16_t)py, (int16_t)TILE, (uint16_t)(v / 2)};
                }
                continue;
            }

            int px = dx, py = dy;
            if (info && info->fromX[y][x] >= 0) {
                px = dx + (int)((cellX(info->fromX[y][x]) - dx) * (1.0f - t));
                py = dy + (int)((cellY(info->fromY[y][x]) - dy) * (1.0f - t));
            }
            int sz = (info && info->merged[y][x]) ? (int)(TILE * mergeScale) : TILE;
            if (n < maxOut)
                out[n++] = DrawCmd{(int16_t)(px - (sz - TILE) / 2), (int16_t)(py - (sz - TILE) / 2),
                                   (int16_t)sz, v};
        }
    }
    return n;
}

// 在随机空位放一个新数字（约 90% 是 2）。
inline bool spawn(Tiles b, uint32_t r, int *outX = nullptr, int *outY = nullptr)
{
    int n = emptyCount(b);
    if (!n) return false;
    int idx = (int)(r % (uint32_t)n);
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            if (b[y][x]) continue;
            if (idx-- == 0) {
                b[y][x] = ((r >> 16) % 10 == 0) ? 4 : 2;
                if (outX) *outX = x;
                if (outY) *outY = y;
                return true;
            }
        }
    }
    return false;
}

}  // namespace g2048
