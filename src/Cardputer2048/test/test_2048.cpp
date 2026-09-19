// 在 PC 上跑 2048 核心逻辑自检：g++ -std=c++11 -o /tmp/t2048 test/test_2048.cpp && /tmp/t2048
#include "../game2048.h"

#include <cassert>
#include <cstdio>

using namespace g2048;

static void load(Tiles b, const uint16_t v[16])
{
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) b[y][x] = v[y * N + x];
}

static bool rowIs(const Tiles b, int y, uint16_t a, uint16_t c, uint16_t d, uint16_t e)
{
    return b[y][0] == a && b[y][1] == c && b[y][2] == d && b[y][3] == e;
}

static bool colIs(const Tiles b, int x, uint16_t a, uint16_t c, uint16_t d, uint16_t e)
{
    return b[0][x] == a && b[1][x] == c && b[2][x] == d && b[3][x] == e;
}

int main()
{
    Tiles b;
    uint32_t s = 0;

    // 左移合并：2 2 4 0 -> 4 4 0 0（新合成的 4 不能和原来的 4 再合并）
    load(b, (const uint16_t[16]){2, 2, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    assert(move(b, LEFT, s) && s == 4 && rowIs(b, 0, 4, 4, 0, 0));

    // 四个相同：2 2 2 2 -> 4 4 0 0
    load(b, (const uint16_t[16]){2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    assert(move(b, LEFT, s) && s == 8 && rowIs(b, 0, 4, 4, 0, 0));

    // 不能连环合并：4 4 8 8 -> 8 16 0 0
    load(b, (const uint16_t[16]){4, 4, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    assert(move(b, LEFT, s) && s == 24 && rowIs(b, 0, 8, 16, 0, 0));

    // 右移靠边：2 0 2 4 -> 0 0 4 4
    load(b, (const uint16_t[16]){2, 0, 2, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    assert(move(b, RIGHT, s) && s == 4 && rowIs(b, 0, 0, 0, 4, 4));

    // 上移（整列 2 2 2 2 -> 4 4 0 0）
    load(b, (const uint16_t[16]){2, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0});
    assert(move(b, UP, s) && s == 8 && colIs(b, 0, 4, 4, 0, 0));

    // 下移（整列 2 2 4 0 -> 0 0 4 4）
    load(b, (const uint16_t[16]){2, 0, 0, 0, 2, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0});
    assert(move(b, DOWN, s) && s == 4 && colIs(b, 0, 0, 0, 4, 4));

    // 无效移动：棋盘不变，返回 false
    load(b, (const uint16_t[16]){2, 4, 2, 4, 4, 2, 4, 2, 2, 4, 2, 4, 4, 2, 4, 2});
    Tiles before;
    memcpy(before, b, sizeof(Tiles));
    assert(!move(b, LEFT, s) && s == 0 && memcmp(before, b, sizeof(Tiles)) == 0);
    assert(!canMove(b) && maxTile(b) == 4);

    // 动画信息：2 2 4 0 向左 -> (0,0) 是两个 2 撞出来的，(0,1) 是从第 2 列滑过来的 4
    load(b, (const uint16_t[16]){2, 2, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    MoveInfo info;
    assert(move(b, LEFT, s, &info) && rowIs(b, 0, 4, 4, 0, 0));
    assert(info.merged[0][0] && info.fromX[0][0] == 0 && info.from2X[0][0] == 1);
    assert(!info.merged[0][1] && info.fromX[0][1] == 2 && info.fromY[0][1] == 0);
    assert(info.fromX[0][2] == -1 && !info.merged[0][2]);  // 空格没有来源

    // 一帧最多画 16 个方块 + 最多 8 个合并的第二块
    Tiles full;
    load(full, (const uint16_t[16]){2, 2, 4, 4, 8, 8, 16, 16, 32, 32, 64, 64, 128, 128, 2, 4});
    MoveInfo finfo;
    move(full, LEFT, s, &finfo);
    DrawCmd cmds[24];
    for (int f = 0; f <= g2048::SLIDE_STEPS; ++f)
        assert(frameTiles(full, &finfo, (float)f / g2048::SLIDE_STEPS, -1, -1, 1.0f, 1.0f, cmds, 24) <= 24);

    // 满盘但有一对相邻相同 -> 还能动
    load(b, (const uint16_t[16]){2, 4, 2, 4, 4, 2, 4, 2, 2, 4, 2, 4, 4, 2, 2, 2});
    assert(canMove(b));

    // 出数字：只落在空位，且只会是 2 / 4
    clearBoard(b);
    b[0][0] = 2;
    assert(spawn(b, 12345) && emptyCount(b) == 14);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
            if ((x || y) && b[y][x]) assert(b[y][x] == 2 || b[y][x] == 4);
    for (int i = 1; i < 16; ++i) spawn(b, (uint32_t)i * 7919);
    assert(emptyCount(b) == 0 && !spawn(b, 1));

    printf("game2048: all checks passed\n");
    return 0;
}
