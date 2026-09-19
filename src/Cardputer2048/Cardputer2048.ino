/*
 * Cardputer ADV 简易 2048
 *
 * 按键：; (上) , (左) . (下) / (右)，单独按即可，不用 Fn；R 重开一局。
 * 依赖：M5Unified / M5GFX / M5Cardputer（Arduino 库管理器里装齐即可）。
 */
#include <M5Cardputer.h>
#include <Preferences.h>

#include "game2048.h"

static constexpr int PANEL_X  = 136;   // 右侧信息栏左边界
static constexpr int PANEL_CX = 188;   // 右侧信息栏中心

// 棋盘画在内存画布上，一帧只推一次，避免看到"先铺底再画块"的中间状态
static constexpr int CV_X = g2048::BOARD_X - 3;
static constexpr int CV_Y = g2048::BOARD_Y - 3;
static constexpr int CV_W = g2048::BOARD_W + 6;
static M5Canvas      canvas(&M5Cardputer.Display);

// 右侧信息栏也走画布，免得"先清空再写字"闪一下
static constexpr int PV_X = PANEL_X - 4;  // 起点留到棋盘画布右边界（x=131）之后，两块画布之间不留缝
static constexpr int PV_W = 240 - PV_X;
static M5Canvas      panelCanvas(&M5Cardputer.Display);

// ---------------- 配色 ----------------
static uint16_t C_BG, C_TRAY, C_EMPTY, C_LABEL, C_VALUE, C_TITLE, C_GOOD, C_BAD;

// ---------------- 状态 ----------------
static g2048::Tiles board;
static uint32_t score = 0;
static uint32_t best  = 0;
static bool gameOver = false;
static bool won      = false;
static bool dirty    = false;  // 局面变了还没落盘
static bool askingResume = false;
static uint8_t brightness = 90;

// 撤销：只留上一步的快照（DEL 用）
struct Snapshot {
    g2048::Tiles tiles;
    uint32_t     score;
    bool         won;
    bool         valid;
};
static Snapshot undoSnap;

static void undo()
{
    if (!undoSnap.valid) return;
    memcpy(board, undoSnap.tiles, sizeof(board));
    score          = undoSnap.score;
    won            = undoSnap.won;
    gameOver       = false;  // 撤掉那一步就活了
    undoSnap.valid = false;  // 只能撤一步
    dirty          = true;
    redraw();
}

static void bumpBrightness(int delta)
{
    int v      = (int)brightness + delta;
    brightness = (uint8_t)(v < 20 ? 20 : (v > 255 ? 255 : v));
    M5Cardputer.Display.setBrightness(brightness);
}

// ---------------- 存档（nvs，20KB 分区）----------------
static Preferences prefs;

struct SaveBlob {
    uint16_t magic;
    uint16_t tiles[g2048::N][g2048::N];
    uint32_t score;
    uint8_t  flags;  // bit0 已结束、bit1 已赢过 2048
};
static const uint16_t SAVE_MAGIC = 0x2048;

static void saveState()
{
    SaveBlob s;
    s.magic = SAVE_MAGIC;
    memcpy(s.tiles, board, sizeof(s.tiles));
    s.score = score;
    s.flags = (gameOver ? 1 : 0) | (won ? 2 : 0);
    prefs.putBytes("state", &s, sizeof(s));
    dirty = false;
}

// 只有"有效且没结束"的存档才值得续：上一局已经 GAME OVER 就直接开新局
static bool loadState()
{
    SaveBlob s;
    if (prefs.getBytesLength("state") != sizeof(s)) return false;
    if (prefs.getBytes("state", &s, sizeof(s)) != sizeof(s)) return false;
    if (s.magic != SAVE_MAGIC || (s.flags & 1)) return false;
    memcpy(board, s.tiles, sizeof(board));
    score    = s.score;
    gameOver = false;
    won      = (s.flags & 2) != 0;
    undoSnap.valid = false;  // 续玩的局面没有可撤的一步
    return true;
}

// ---------------- 电量 / 充电趋势 ----------------
// ADV 没把充电状态接到 MCU，只能看电池电压的趋势：插上充电器瞬间电池端电压会跳高，拔掉会掉下来。
static int  batMv    = 0;  // 平滑后的电池电压
static int  batLast  = 0;  // 上次采样值
static int  batRef   = 0;  // 30 秒前用于比较的值
static int  batTicks = 0;
static bool charging = false;

static void batteryTick()
{
    int mv = 0;
    for (int i = 0; i < 4; ++i) mv += M5.Power.getBatteryVoltage();  // 多读几次压掉 ADC 噪声
    mv /= 4;
    if (mv <= 0) return;

    batMv = batMv ? (batMv * 3 + mv) / 4 : mv;
    if (batLast) {
        int d5 = batMv - batLast;  // 5 秒内的跳变
        if (d5 >= 25) charging = true;
        else if (d5 <= -25) charging = false;
    }
    batLast = batMv;

    if (++batTicks >= 6) {  // 每 30 秒再看一次长趋势，慢速上升也能认出来
        batTicks = 0;
        if (batRef) {
            int d = batMv - batRef;
            if (d >= 15) charging = true;
            else if (d <= -15) charging = false;
        }
        batRef = batMv;
    }
}

static uint16_t rgb(uint32_t c)
{
    return M5Cardputer.Display.color565((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff);
}

// 在像素坐标画一个方块（size 不一定是 TILE，动画时要缩放；空位不走这里）
static void drawTileBox(int px, int py, int size, uint16_t v)
{
    auto &d = canvas;
    px -= CV_X;
    py -= CV_Y;
    d.fillRoundRect(px, py, size, size, 5, rgb(g2048::tileRgb(v)));
    char s[8];
    snprintf(s, sizeof(s), "%u", (unsigned)v);
    d.setTextDatum(middle_center);
    d.setTextColor(v <= 4 ? rgb(0x776e65) : rgb(0xf9f6f2));
    d.setTextSize(strlen(s) <= 2 ? 2 : 1);
    d.drawString(s, px + size / 2, py + size / 2);
}

// 画一帧：哪些方块画在哪由 game2048.h 的 frameTiles() 算，这里只管画
static void drawFrame(const g2048::MoveInfo *info, float t, int spawnX, int spawnY, float mergeScale,
                      float spawnScale)
{
    canvas.fillSprite(C_BG);
    canvas.fillRoundRect(0, 0, CV_W, CV_W, 7, C_TRAY);
    for (int y = 0; y < g2048::N; ++y)
        for (int x = 0; x < g2048::N; ++x)
            if (!board[y][x])
                canvas.fillRoundRect(g2048::cellX(x) - CV_X, g2048::cellY(y) - CV_Y, g2048::TILE,
                                     g2048::TILE, 5, C_EMPTY);

    g2048::DrawCmd cmds[24];
    int n = g2048::frameTiles(board, info, t, spawnX, spawnY, mergeScale, spawnScale, cmds, 24);
    for (int i = 0; i < n; ++i) drawTileBox(cmds[i].x, cmds[i].y, cmds[i].size, cmds[i].value);
    canvas.pushSprite(CV_X, CV_Y);
}

// 电量图标 + 百分比（充电中再加个 CHG），右对齐到 rightX（屏幕右上角）
static void drawBattery(int rightX, int cy, int level, bool isCharging)
{
    auto &d = panelCanvas;
    const int w = 20, h = 10;
    uint16_t  col = level < 0 ? C_LABEL : (level > 50 ? C_GOOD : (level > 20 ? C_TITLE : C_BAD));
    int       x   = rightX - (w + 7 + 54);  // 54 = " 100% CHG" 的宽度，两种状态图标位置都不动

    d.drawRect(x, cy - h / 2, w, h, col);
    d.fillRect(x + w, cy - 2, 3, 4, col);
    if (level > 0) {
        int fw = (w - 4) * (level > 100 ? 100 : level) / 100;
        if (fw > 0) d.fillRect(x + 2, cy - h / 2 + 2, fw, h - 4, col);
    }

    char buf[16];
    if (level < 0) snprintf(buf, sizeof(buf), " --");
    else if (isCharging) snprintf(buf, sizeof(buf), " %d%% CHG", level);
    else snprintf(buf, sizeof(buf), " %d%%", level);
    d.setTextSize(1);
    d.setTextColor(isCharging ? C_GOOD : col);
    d.setTextDatum(middle_left);
    d.drawString(buf, x + w + 7, cy);
    d.setTextDatum(middle_center);
}

static void drawPanel()
{
    auto &d = panelCanvas;
    char buf[16];

    d.fillSprite(C_BG);
    int cx = PANEL_CX - PV_X;
    d.setTextDatum(middle_center);
    d.setTextColor(C_TITLE);
    d.setTextSize(2);
    d.drawString("2048", cx, 26);

    d.setTextSize(1);
    d.setTextColor(C_LABEL);
    d.drawString("SCORE", cx, 46);
    d.setTextSize(2);
    d.setTextColor(C_VALUE);
    snprintf(buf, sizeof(buf), "%u", (unsigned)score);
    d.drawString(buf, cx, 60);

    d.setTextSize(1);
    d.setTextColor(C_LABEL);
    d.drawString("BEST", cx, 78);
    d.setTextSize(2);
    d.setTextColor(C_VALUE);
    snprintf(buf, sizeof(buf), "%u", (unsigned)best);
    d.drawString(buf, cx, 90);

    drawBattery(PV_W - 4, 10, M5.Power.getBatteryLevel(), charging);

    d.setTextSize(1);
    d.setTextColor(C_LABEL);
    d.drawString("arrows: ; , . /", cx, 102);
    d.drawString("DEL undo  R new", cx, 114);
    if (gameOver || won) {
        d.setTextColor(gameOver ? C_BAD : C_GOOD);
        d.drawString(gameOver ? "GAME OVER" : "YOU WIN!", cx, 126);
    } else {
        d.setTextColor(C_LABEL);
        d.drawString("-/+ light", cx, 126);
    }
    d.pushSprite(PV_X, 0);
}

static void redraw()
{
    auto &d = M5Cardputer.Display;
    drawFrame(nullptr, 1.0f, -1, -1, 1.0f, 1.0f);  // 顺带盖掉上一次的 GAME OVER 边框
    drawPanel();

    if (gameOver) {
        int w = g2048::BOARD_W - 8, x = g2048::BOARD_X + 4, y = g2048::BOARD_Y + g2048::BOARD_W / 2 - 20;
        d.fillRoundRect(x, y, w, 40, 6, C_BG);
        d.drawRoundRect(x, y, w, 40, 6, C_BAD);
        d.setTextDatum(middle_center);  // 主屏幕默认是左上角对齐，这里必须改成居中
        d.setTextColor(C_BAD);
        d.setTextSize(2);
        d.drawString("GAME OVER", x + w / 2, y + 14);
        d.setTextSize(1);
        d.setTextColor(C_VALUE);
        d.drawString("R: new game", x + w / 2, y + 30);
    } else if (askingResume) {
        int w = g2048::BOARD_W - 8, x = g2048::BOARD_X + 4, y = g2048::BOARD_Y + g2048::BOARD_W / 2 - 32;
        d.fillRoundRect(x, y, w, 64, 6, C_BG);
        d.drawRoundRect(x, y, w, 64, 6, C_TITLE);
        d.setTextDatum(middle_center);
        d.setTextColor(C_TITLE);
        d.setTextSize(1);
        d.drawString("SAVED GAME FOUND", x + w / 2, y + 14);
        d.setTextColor(C_VALUE);
        d.drawString("ENTER: continue", x + w / 2, y + 34);
        d.setTextColor(C_LABEL);
        d.drawString("R: new game", x + w / 2, y + 50);
    }
}

static void animate(const g2048::MoveInfo &info, int spawnX, int spawnY)
{
    for (int f = 1; f <= g2048::SLIDE_STEPS; ++f) {
        drawFrame(&info, (float)f / g2048::SLIDE_STEPS, spawnX, spawnY, 1.0f, 0.0f);
        delay(g2048::FRAME_MS);
    }
    for (int f = 0; f < g2048::POP_STEPS; ++f) {
        drawFrame(&info, 1.0f, spawnX, spawnY, g2048::POP_SCALE[f], g2048::SPAWN_SCALE[f]);
        delay(g2048::FRAME_MS);
    }
}

static void resetGame()
{
    g2048::clearBoard(board);
    score    = 0;
    gameOver = false;
    won      = false;
    dirty    = true;
    undoSnap.valid = false;
    g2048::spawn(board, esp_random());
    g2048::spawn(board, esp_random());
}

static void newGame()
{
    resetGame();
    askingResume = false;
    redraw();
}

static void doMove(int dir)
{
    if (gameOver) return;
    g2048::Tiles before;
    memcpy(before, board, sizeof(board));
    uint32_t beforeScore = score;
    bool     beforeWon   = won;

    g2048::MoveInfo info;
    uint32_t gained = 0;
    if (!g2048::move(board, dir, gained, &info)) return;

    memcpy(undoSnap.tiles, before, sizeof(board));  // 这一手之前的局面，DEL 可以撤
    undoSnap.score = beforeScore;
    undoSnap.won   = beforeWon;
    undoSnap.valid = true;

    score += gained;
    if (score > best) {
        best = score;
        prefs.putUInt("best", best);
    }
    if (g2048::maxTile(board) >= 2048) won = true;
    int spawnX = -1, spawnY = -1;
    g2048::spawn(board, esp_random(), &spawnX, &spawnY);
    if (!g2048::canMove(board)) gameOver = true;
    dirty = true;

    animate(info, spawnX, spawnY);
    redraw();
}

static void handleKeys()
{
    auto &kb = M5Cardputer.Keyboard;
    if (!kb.isChange()) return;

    if (askingResume) {  // 开机选择：回车继续上一局，R 开新局
        if (kb.keysState().enter) {
            askingResume = false;
            redraw();
        } else if (kb.isKeyPressed('r') || kb.isKeyPressed('R')) {
            newGame();
        }
        return;
    }

    // 键盘右下角印箭头的那四个键，直接按
    int dir = 0;
    if (kb.isKeyPressed(';')) dir = g2048::UP;
    else if (kb.isKeyPressed(',')) dir = g2048::LEFT;
    else if (kb.isKeyPressed('.')) dir = g2048::DOWN;
    else if (kb.isKeyPressed('/')) dir = g2048::RIGHT;

    if (dir) doMove(dir);
    else if (kb.keysState().del) undo();
    else if (kb.isKeyPressed('-') || kb.isKeyPressed('_')) bumpBrightness(-25);
    else if (kb.isKeyPressed('+') || kb.isKeyPressed('=')) bumpBrightness(+25);
    else if (kb.isKeyPressed('r') || kb.isKeyPressed('R') || kb.keysState().enter) newGame();
}

void setup()
{
    auto cfg           = M5.config();
    cfg.fallback_board = m5gfx::board_t::board_M5CardputerADV;  // 自动识别失败时兜底
    M5Cardputer.begin(cfg, true);

    auto &d = M5Cardputer.Display;
    d.setRotation(1);
    if (d.height() > d.width()) d.setRotation(3);  // 保证横屏 240x135
    d.setBrightness(brightness);

    C_BG    = rgb(0x222222);
    C_TRAY  = rgb(0x333333);
    C_EMPTY = rgb(0x4a4a4a);
    C_LABEL = rgb(0x999999);
    C_VALUE = rgb(0xffffff);
    C_TITLE = rgb(0xedc22e);
    C_GOOD  = rgb(0x6fcf5f);
    C_BAD   = rgb(0xe85a4a);

    d.fillScreen(C_BG);
    canvas.setColorDepth(16);
    canvas.createSprite(CV_W, CV_W);
    panelCanvas.setColorDepth(16);
    panelCanvas.createSprite(PV_W, 135);
    batteryTick();  // 开机先采一次，免得第一屏显示 --

    prefs.begin("g2048", false);
    best        = prefs.getUInt("best", 0);
    resetGame();
    askingResume = loadState();  // 有没打完的存档就切过去，并弹选择
    redraw();
}

void loop()
{
    M5Cardputer.update();
    handleKeys();

    // 挂着不动时也刷新电量：5 秒采一次电压，顺便重画信息栏
    static uint32_t lastRefreshMs = 0;
    if (millis() - lastRefreshMs > 5000) {
        lastRefreshMs = millis();
        batteryTick();
        if (dirty) saveState();  // 节流落盘：最坏丢最后几秒的操作
        drawPanel();
    }
}
