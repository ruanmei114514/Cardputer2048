// 本地跑一遍动画并输出每一帧（PPM），用来在电脑上看效果，不用上机。
//   g++ -std=c++11 -o /tmp/p2048 test/preview_2048.cpp && /tmp/p2048 /tmp/cp2048_preview
// 帧的绘制内容来自 game2048.h 的 frameTiles()，和固件用的是同一份算法。
#include "../game2048.h"

#include <cstdio>
#include <cstring>

using namespace g2048;

static const int W = 240, H = 135;
static uint8_t   fb[H][W][3];
static Tiles     board;

static const uint8_t DIG[10][5] = {{7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7},
                                   {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1},
                                   {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}};

static void put(int x, int y, uint32_t c)
{
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    fb[y][x][0] = (c >> 16) & 0xff;
    fb[y][x][1] = (c >> 8) & 0xff;
    fb[y][x][2] = c & 0xff;
}

static void fillRect(int x, int y, int w, int h, uint32_t c)
{
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) put(x + i, y + j, c);
}

// 四角切掉 2px，近似板子上的圆角
static void box(int x, int y, int size, uint32_t c)
{
    fillRect(x + 2, y, size - 4, size, c);
    fillRect(x, y + 2, size, size - 4, c);
}

static void drawNumber(int cx, int cy, uint16_t v, uint32_t color)
{
    char s[8];
    snprintf(s, sizeof(s), "%u", (unsigned)v);
    int len   = (int)strlen(s);
    int scale = len <= 2 ? 3 : 2;
    int gw    = 4 * scale;  // 字宽 3 + 间距 1
    int ox    = cx - (len * gw - scale) / 2;
    int oy    = cy - 5 * scale / 2;
    for (int i = 0; i < len; ++i) {
        const uint8_t *g = DIG[s[i] - '0'];
        for (int r = 0; r < 5; ++r)
            for (int c = 0; c < 3; ++c)
                if (g[r] & (1 << (2 - c))) fillRect(ox + i * gw + c * scale, oy + r * scale, scale, scale, color);
    }
}

static uint32_t rngState = 20260918u;
static uint32_t rng()
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return rngState;
}

static FILE *indexFile = nullptr;

static void emitFrame(const char *dir, int idx, const MoveInfo *info, float t, int spawnX, int spawnY,
                      float mergeScale, float spawnScale, uint32_t score, int moveId, int merges)
{
    fillRect(0, 0, W, H, 0x222222);
    box(BOARD_X - 3, BOARD_Y - 3, BOARD_W + 6, 0x333333);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
            if (!board[y][x]) box(cellX(x), cellY(y), TILE, 0x4a4a4a);

    DrawCmd cmds[24];
    int n = frameTiles(board, info, t, spawnX, spawnY, mergeScale, spawnScale, cmds, 24);
    for (int i = 0; i < n; ++i) {
        const DrawCmd &d = cmds[i];
        box(d.x, d.y, d.size, tileRgb(d.value));
        drawNumber(d.x + d.size / 2, d.y + d.size / 2, d.value, d.value <= 4 ? 0x776e65 : 0xf9f6f2);
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/frame_%03d.ppm", dir, idx);
    FILE *f = fopen(path, "wb");
    if (!f) {
        printf("cannot write %s\n", path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    fwrite(fb, 1, sizeof(fb), f);
    fclose(f);
    fprintf(indexFile, "%d %u %d %d\n", idx, (unsigned)score, moveId, merges);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "/tmp/cp2048_preview";
    char path[512];
    snprintf(path, sizeof(path), "%s/frames.txt", dir);
    indexFile = fopen(path, "w");
    if (!indexFile) {
        printf("cannot write %s\n", path);
        return 1;
    }

    clearBoard(board);
    spawn(board, rng());
    spawn(board, rng());
    int idx = 0;
    uint32_t score = 0;
    emitFrame(dir, idx++, nullptr, 1.0f, -1, -1, 1.0f, 1.0f, score, 0, 0);

    // 固定顺序走一圈，把棋盘铺开，动画才看得出东西
    const int moves[] = {LEFT, UP, RIGHT, DOWN, LEFT, UP, RIGHT, DOWN, LEFT, UP, RIGHT, DOWN};
    for (int m = 0; m < (int)(sizeof(moves) / sizeof(moves[0])); ++m) {
        MoveInfo info;
        uint32_t gained = 0;
        if (!move(board, moves[m], gained, &info)) continue;
        score += gained;
        int spawnX = -1, spawnY = -1;
        spawn(board, rng(), &spawnX, &spawnY);
        int merges = 0;
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x)
                if (info.merged[y][x]) ++merges;
        for (int f = 1; f <= SLIDE_STEPS; ++f)
            emitFrame(dir, idx++, &info, (float)f / SLIDE_STEPS, spawnX, spawnY, 1.0f, 0.0f, score, m + 1, merges);
        for (int f = 0; f < POP_STEPS; ++f)
            emitFrame(dir, idx++, &info, 1.0f, spawnX, spawnY, POP_SCALE[f], SPAWN_SCALE[f], score, m + 1, merges);
    }
    fclose(indexFile);
    printf("wrote %d frames (%d slide + %d pop per move) to %s\n", idx, SLIDE_STEPS, POP_STEPS, dir);
    return 0;
}
