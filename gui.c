/* ===========================================================================
 *  gui.c — Razer BlackWidow Essential 背光控制面板
 * ---------------------------------------------------------------------------
 *  · 三颗胶囊按钮：关 / 常亮 / 呼吸灯，点哪个立即生效
 *  · 睡眠模式：灯光按钮变灰锁定，灯光关闭
 *  · 睡眠定时：展开式时间选择（24 小时制，默认一次，可勾选每天）
 *  · 设置页：最小化至托盘 / 关闭时最小化 / 开机自启（开关立即保存）
 *  · 托盘图标、单实例、深色圆角无边框窗口
 *
 *  编译（MSYS2 x64 + hidapi）：
 *      make gui
 *  或者：
 *      gcc gui.c -lhidapi -lgdi32 -luser32 -ladvapi32 -lshell32 -lmsimg32 -mwindows -o BWBacklight.exe
 * =========================================================================== */

#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <wchar.h>
#include <string.h>
#include <time.h>

#include "main.h"       /* DEV_VID / DEV_PID / DEV_USAGE */

/* --------------------------- 程序标识 ------------------------------------- */
#define APP_CLASS   L"BWBacklightPanelV3"
#define APP_TITLE   L"黑寡妇背光控制"
#define MUTEX_NAME  L"Local\\BWBacklightPanel_9C3F2A7E_V3"
#define APP_ICON_ID 1   /* app.rc 里的图标组 ID；没有资源时退回系统图标 */

/* --------------------------- 布局（96 DPI 逻辑像素） ---------------------- */
#define WIN_W       440
#define TITLE_H     60
#define CORNER      24

#define COLLAPSED_H 344
#define EXPANDED_H  560
#define SETTINGS_H  400

#define LIGHT_X     32
#define LIGHT_Y     96
#define LIGHT_W     120
#define LIGHT_H     48
#define LIGHT_GAP   8

#define ROW_X       32
#define ROW_W       376
#define ROW_H       52
#define SLEEP_Y     160
#define TIMER_Y     224

#define STATUS_Y     292
#define STATUS_Y_EXP 506
#define STATUS_H     28

#define PANEL_Y     288
#define PANEL_H     200

#define STEP_X1     100
#define STEP_X2     250
#define STEP_W      90
#define STEP_UP_Y   (PANEL_Y + 54)
#define STEP_VAL_Y  (PANEL_Y + 80)
#define STEP_DOWN_Y (PANEL_Y + 122)
#define STEP_BTN_H  26
#define STEP_VAL_H  42

#define ACT_Y       448
#define ACT_H       32
#define PRIMARY_X   52
#define PRIMARY_W   130
#define DAILY_X     190
#define DAILY_W     100
#define SECONDARY_X 298
#define SECONDARY_W 94

/* --------------------------- 设置页布局 ------------------------------------ */
#define VIEW_MAIN     0
#define VIEW_SETTINGS 1

#define GEAR_X      (WIN_W - 117)
#define BACK_X      18
#define BACK_Y      16
#define BACK_S      32

#define SET_ROW_X    32
#define SET_ROW_W    376
#define SET_ROW_H    68
#define SET_ROW_GAP  10
#define SET_ROW1_Y   88
#define SET_ROW2_Y   (SET_ROW1_Y + SET_ROW_H + SET_ROW_GAP)
#define SET_ROW3_Y   (SET_ROW2_Y + SET_ROW_H + SET_ROW_GAP)
#define SET_HINT_Y   (SET_ROW3_Y + SET_ROW_H + 20)
#define SET_STATUS_Y 360
#define SET_ICON_DX  28
#define SET_TEXT_DX  56
#define SET_TOG_W    44
#define SET_TOG_H    24
#define SET_TOG_RIGHT 20

#define SS          2

/* --------------------------- 定时器 --------------------------------------- */
#define IDT_CHECK       1
#define IDT_AUTOSTART   2
#define AUTOSTART_TRIES 20   /* 登录后键盘未就绪时，最多每秒重试 20 次 */
#define CONF_KEY    L"Software\\BWEssentialBacklight"
#define TIMER_NEXT  L"SleepTimerNext"
#define TIMER_DAILY L"SleepTimerDaily"
#define OPT_MIN_TRAY   L"MinimizeToTray"
#define OPT_CLOSE_TRAY L"CloseToTray"

/* --------------------------- 托盘 ----------------------------------------- */
#define WM_TRAY      (WM_APP + 1)
#define TRAY_ID      1
#define TRAY_OPEN    1
#define TRAY_EXIT    2

/* --------------------------- HID 报文 ------------------------------------- */
#define REPORT_LEN      91
#define RX_CLASS        0x0F
#define RX_SET_EFFECT   0x02
#define RX_GET_EFFECT   0x82
#define RX_SET_BRIGHT   0x04
#define VARSTORE        0x01
#define LED_BACKLIGHT   0x05
#define EFF_OFF         0x00
#define EFF_STATIC      0x01
#define EFF_BREATH      0x02
#define COLOR_R         0x00
#define COLOR_G         0xFF
#define COLOR_B         0x00

/* --------------------------- 开机自启 ------------------------------------- */
#define RUN_KEY  L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VAL  L"BWEssentialBacklight"

/* --------------------------- 状态 ----------------------------------------- */
enum { LIGHT_OFF = 0, LIGHT_ON = 1, LIGHT_BREATH = 2 };

enum {
    HIT_NONE = -1,
    HIT_LIGHT0 = 0,
    HIT_LIGHT1 = 1,
    HIT_LIGHT2 = 2,
    HIT_SLEEP = 3,
    HIT_TIMER = 4,
    HIT_CLOSE = 6,
    HIT_MIN = 7,
    HIT_HOUR_UP = 8,
    HIT_HOUR_DOWN = 9,
    HIT_MIN_UP = 10,
    HIT_MIN_DOWN = 11,
    HIT_TIMER_PRIMARY = 12,
    HIT_TIMER_SECONDARY = 13,
    HIT_TIMER_DAILY = 14,
    HIT_SETTINGS = 15,
    HIT_BACK = 16,
    HIT_OPT_MIN = 17,
    HIT_OPT_CLOSE = 18,
    HIT_OPT_AUTO = 19
};

/* --------------------------- 配色 ----------------------------------------- */
#define C_BG          RGB( 18,  19,  24)
#define C_BG_TOP      RGB( 28,  30,  38)
#define C_EDGE        RGB( 58,  61,  73)
#define C_TITLE       RGB(238, 240, 246)
#define C_SUB         RGB(130, 136, 152)
#define C_STATUS      RGB(155, 161, 176)
#define C_ERR         RGB(255, 120, 110)

#define C_PILL        RGB( 43,  46,  56)
#define C_PILL_H      RGB( 53,  57,  69)
#define C_PILL_P      RGB( 36,  39,  48)
#define C_PILL_ED     RGB( 69,  74,  89)
#define C_TXT_OFF     RGB(222, 226, 236)

#define C_DISABLED    RGB( 34,  36,  43)
#define C_DISABLED_ED RGB( 50,  53,  63)
#define C_DISABLED_TX RGB( 96, 101, 115)

#define C_ACCENT      RGB( 68, 214,  44)
#define C_ACCENT_H    RGB( 92, 232,  70)
#define C_ACCENT_P    RGB( 54, 180,  34)
#define C_ACCENT_ED   RGB(120, 240, 100)
#define C_ACCENT_TX   RGB(  8,  32,   6)

#define C_PANEL       RGB( 31,  33,  41)
#define C_PANEL_ED    RGB( 52,  55,  66)

#define C_TB_IDLE     RGB( 29,  31,  39)
#define C_TB_HOVER    RGB( 48,  51,  61)
#define C_TB_PRESS    RGB( 40,  43,  52)

#define C_OPT_H       RGB( 38,  41,  50)
#define C_OPT_P       RGB( 26,  28,  35)
#define C_SW_OFF      RGB( 60,  64,  75)
#define C_KNOB        RGB(245, 247, 251)

#define C_GREEN       RGB( 68, 214,  44)

/* --------------------------- 全局状态 ------------------------------------- */
static HWND   g_hwnd = NULL;
static int    g_dpi  = 96;

static int    g_client_w = 0, g_client_h = 0;
static int    g_buf_w = 0, g_buf_h = 0;
static HDC    g_memdc = NULL;
static HBITMAP g_bmp = NULL;
static HGDIOBJ g_oldbmp = NULL;

static HFONT  g_font_title = NULL;
static HFONT  g_font_sub = NULL;
static HFONT  g_font_label = NULL;
static HFONT  g_font_hint = NULL;
static HFONT  g_font_capsule = NULL;
static HFONT  g_font_pill = NULL;
static HFONT  g_font_big = NULL;
static HFONT  g_font_action = NULL;
static HFONT  g_font_status = NULL;

static int    g_light = LIGHT_OFF;
static int    g_sleeping = 0;
static int    g_auto = 0;
static int    g_view = VIEW_MAIN;
static int    g_min_to_tray = 1;
static int    g_close_to_tray = 0;
static int    g_tray_added = 0;
static int    g_quitting = 0;

static int    g_timer_on = 0;
static int    g_timer_daily = 0;
static int    g_timer_hour = 23;
static int    g_timer_min = 0;
static int    g_edit_hour = 23;
static int    g_edit_min = 0;
static int    g_edit_daily = 0;
static time_t g_next_fire = 0;
static int    g_timer_open = 0;

static int    g_hover = HIT_NONE;
static int    g_press = HIT_NONE;
static int    g_tracking = 0;
static int    g_dragging = 0;
static POINT  g_drag_cursor;
static RECT   g_drag_window;

static wchar_t g_status[160] = L"就绪";
static int     g_status_err = 0;
static int     g_autostart_tries = 0;

/* --------------------------- 前向声明 ------------------------------------- */
static void resize_for_timer(HWND hwnd, int open);
static void update_region(HWND hwnd);
static void destroy_backbuffer(void);
static int  create_backbuffer(HWND hwnd);
static void set_status(const wchar_t *text, int is_error);
static time_t compute_next_fire(int hour, int minute, int *is_tomorrow);

/* --------------------------- 小工具 --------------------------------------- */
static int D(int v) { return MulDiv(v, g_dpi, 96); }
static int S(int v) { return D(v) * SS; }

static RECT R(int x, int y, int w, int h)
{
    RECT r;
    r.left   = D(x);
    r.top    = D(y);
    r.right  = D(x + w);
    r.bottom = D(y + h);
    return r;
}

static RECT R4(int x, int y, int w, int h)
{
    RECT r;
    r.left   = D(x) * SS;
    r.top    = D(y) * SS;
    r.right  = D(x + w) * SS;
    r.bottom = D(y + h) * SS;
    return r;
}

static RECT back_rect(void) { return R(BACK_X, BACK_Y, BACK_S, BACK_S); }
static RECT gear_rect(void) { return R(WIN_W - 178, 17, 91, 30); }

static RECT set_row_rect(int i)
{
    return R(SET_ROW_X, SET_ROW1_Y + i * (SET_ROW_H + SET_ROW_GAP),
             SET_ROW_W, SET_ROW_H);
}

static RECT set_toggle_rect(int i)
{
    RECT r = set_row_rect(i);
    RECT t;
    int cy = (r.top + r.bottom) / 2;

    t.left   = r.right - D(SET_TOG_RIGHT) - D(SET_TOG_W);
    t.right  = r.right - D(SET_TOG_RIGHT);
    t.top    = cy - D(SET_TOG_H) / 2;
    t.bottom = cy + D(SET_TOG_H) / 2;
    return t;
}

/* --------------------------- GDI 绘制 ------------------------------------- */
static void fill_rect(HDC dc, RECT r, COLORREF color)
{
    HBRUSH br = CreateSolidBrush(color);
    FillRect(dc, &r, br);
    DeleteObject(br);
}

static void fill_ellipse(HDC dc, int cx, int cy, int rx, int ry, COLORREF color)
{
    HBRUSH br = CreateSolidBrush(color);
    HGDIOBJ old_br = SelectObject(dc, br);
    HGDIOBJ old_pen = SelectObject(dc, GetStockObject(NULL_PEN));

    Ellipse(dc, cx - rx, cy - ry, cx + rx, cy + ry);

    SelectObject(dc, old_pen);
    SelectObject(dc, old_br);
    DeleteObject(br);
}

static void stroke_ellipse(HDC dc, int cx, int cy, int rx, int ry,
                           COLORREF color, int width)
{
    HPEN pen = CreatePen(PS_SOLID, width, color);
    if (!pen) return;

    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_br = SelectObject(dc, GetStockObject(NULL_BRUSH));

    Ellipse(dc, cx - rx, cy - ry, cx + rx, cy + ry);

    SelectObject(dc, old_br);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

static void round_fill(HDC dc, RECT r, int radius,
                       COLORREF fill, COLORREF edge, int edge_w)
{
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = (edge_w > 0) ? CreatePen(PS_SOLID, edge_w, edge)
                            : (HPEN)GetStockObject(NULL_PEN);
    HGDIOBJ old_br = SelectObject(dc, br);
    HGDIOBJ old_pen = SelectObject(dc, pen);

    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius * 2, radius * 2);

    SelectObject(dc, old_pen);
    SelectObject(dc, old_br);
    DeleteObject(br);
    if (edge_w > 0) DeleteObject(pen);
}

static void round_outline(HDC dc, RECT r, int radius,
                          COLORREF edge, int width)
{
    HPEN pen = CreatePen(PS_SOLID, width, edge);
    if (!pen) return;

    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_br = SelectObject(dc, GetStockObject(NULL_BRUSH));

    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius * 2, radius * 2);

    SelectObject(dc, old_br);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

static void draw_line(HDC dc, int x1, int y1, int x2, int y2,
                      COLORREF color, int width)
{
    HPEN pen = CreatePen(PS_SOLID, width, color);
    if (!pen) return;

    HGDIOBJ old_pen = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, NULL);
    LineTo(dc, x2, y2);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

/* dir: -1 上, 0 右, 1 下, 2 左 */
static void draw_chevron(HDC dc, int cx, int cy, COLORREF color, int dir)
{
    int d = S(5);
    int w = SS + SS / 2;

    if (dir < 0) {
        draw_line(dc, cx - d, cy + d / 2, cx, cy - d / 2, color, w);
        draw_line(dc, cx, cy - d / 2, cx + d, cy + d / 2, color, w);
    } else if (dir == 0) {
        draw_line(dc, cx - d / 2, cy - d, cx + d / 2, cy, color, w);
        draw_line(dc, cx + d / 2, cy, cx - d / 2, cy + d, color, w);
    } else if (dir == 1) {
        draw_line(dc, cx - d, cy - d / 2, cx, cy + d / 2, color, w);
        draw_line(dc, cx, cy + d / 2, cx + d, cy - d / 2, color, w);
    } else {
        draw_line(dc, cx + d / 2, cy - d, cx - d / 2, cy, color, w);
        draw_line(dc, cx - d / 2, cy, cx + d / 2, cy + d, color, w);
    }
}

static void draw_moon(HDC dc, int cx, int cy, int r,
                      COLORREF color, COLORREF cutout)
{
    fill_ellipse(dc, cx, cy, r, r, color);
    fill_ellipse(dc, cx + r / 2, cy - r / 2, r, r, cutout);
}

static void draw_clock(HDC dc, int cx, int cy, int r, COLORREF color)
{
    int w = SS + SS / 2;
    stroke_ellipse(dc, cx, cy, r, r, color, w);
    draw_line(dc, cx, cy, cx, cy - r / 2, color, w);
    draw_line(dc, cx, cy, cx + r / 2, cy, color, w);
}

static void draw_power(HDC dc, int cx, int cy, int r,
                       COLORREF color, COLORREF cutout)
{
    int w = SS + SS / 2;
    RECT erase;

    stroke_ellipse(dc, cx, cy, r, r, color, w);

    erase.left = cx - r;
    erase.top = cy - r - S(2);
    erase.right = cx + r;
    erase.bottom = cy - r / 2;
    fill_rect(dc, erase, cutout);

    draw_line(dc, cx, cy - r, cx, cy + r / 2, color, w);
}

static HFONT make_font(int pt, int weight)
{
    return CreateFontW(-MulDiv(pt, g_dpi, 72), 0, 0, 0, weight,
                       FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       L"Microsoft YaHei UI");
}

static void draw_gear(HDC dc, int cx, int cy, int r, COLORREF color)
{
    static const int dir[8][2] = {
        {1000, 0}, {707, 707}, {0, 1000}, {-707, 707},
        {-1000, 0}, {-707, -707}, {0, -1000}, {707, -707}
    };
    int w = SS + SS / 2;
    int i;

    for (i = 0; i < 8; ++i) {
        int x1 = cx + dir[i][0] * (r - S(1)) / 1000;
        int y1 = cy + dir[i][1] * (r - S(1)) / 1000;
        int x2 = cx + dir[i][0] * (r + S(3)) / 1000;
        int y2 = cy + dir[i][1] * (r + S(3)) / 1000;
        draw_line(dc, x1, y1, x2, y2, color, w);
    }
    stroke_ellipse(dc, cx, cy, r, r, color, w);
    stroke_ellipse(dc, cx, cy, r / 2, r / 2, color, w);
}

static void draw_tray_glyph(HDC dc, int cx, int cy, int r, COLORREF color)
{
    int w = SS + SS / 2;

    /* 左下角的托盘落点 */
    draw_line(dc, cx - r, cy - r / 3, cx - r, cy + r, color, w);
    draw_line(dc, cx - r, cy + r, cx + r / 3, cy + r, color, w);

    /* 指向左下角的箭头 */
    draw_line(dc, cx + r, cy - r, cx - r + S(2), cy + r - S(2), color, w);
    draw_line(dc, cx - r + S(2), cy + r - S(2),
              cx - r + S(2) + S(5), cy + r - S(2), color, w);
    draw_line(dc, cx - r + S(2), cy + r - S(2),
              cx - r + S(2), cy + r - S(2) - S(5), color, w);
}

static void draw_box_x(HDC dc, int cx, int cy, int r, COLORREF color)
{
    RECT box;
    int w = SS + SS / 2;
    int k = r / 2;

    box.left   = cx - r;
    box.top    = cy - r;
    box.right  = cx + r;
    box.bottom = cy + r;
    round_outline(dc, box, S(4), color, w);

    draw_line(dc, cx - k, cy - k, cx + k, cy + k, color, w);
    draw_line(dc, cx - k, cy + k, cx + k, cy - k, color, w);
}

static void draw_switch(RECT t, int on, int hover, int press)
{
    RECT tr;
    int h, rad, knob_r, cy, kx;
    COLORREF fill, edge;

    tr.left   = t.left * SS;
    tr.top    = t.top * SS;
    tr.right  = t.right * SS;
    tr.bottom = t.bottom * SS;

    h = tr.bottom - tr.top;
    rad = h / 2;

    if (on) {
        fill = press ? C_ACCENT_P : (hover ? C_ACCENT_H : C_ACCENT);
        edge = C_ACCENT_ED;
    } else {
        fill = press ? C_OPT_P : (hover ? C_OPT_H : C_SW_OFF);
        edge = C_PILL_ED;
    }

    round_fill(g_memdc, tr, rad, fill, edge, SS);

    knob_r = rad - S(3);
    cy = (tr.top + tr.bottom) / 2;
    kx = on ? tr.right - rad : tr.left + rad;
    fill_ellipse(g_memdc, kx, cy, knob_r, knob_r, C_KNOB);
}

static void draw_back_button(void)
{
    int cx = D(BACK_X + BACK_S / 2);
    int cy = D(BACK_Y + BACK_S / 2);
    int rad = D(15);
    COLORREF fill = (g_press == HIT_BACK) ? C_TB_PRESS :
                    (g_hover == HIT_BACK) ? C_TB_HOVER : C_TB_IDLE;

    fill_ellipse(g_memdc, cx * SS, cy * SS, rad * SS, rad * SS, fill);
    draw_chevron(g_memdc, cx * SS, cy * SS,
                 (g_hover == HIT_BACK) ? C_TITLE : C_SUB, 2);
}

static void draw_gear_button(void)
{
    int cx = D(GEAR_X + 15);
    int cy = D(32);
    int rad = D(15);
    COLORREF fill = (g_press == HIT_SETTINGS) ? C_TB_PRESS :
                    (g_hover == HIT_SETTINGS) ? C_TB_HOVER : C_TB_IDLE;

    fill_ellipse(g_memdc, cx * SS, cy * SS, rad * SS, rad * SS, fill);
    draw_gear(g_memdc, cx * SS, cy * SS, S(7),
              (g_hover == HIT_SETTINGS) ? C_TITLE : C_SUB);
}

static void draw_settings_page(void)
{
    int i;

    for (i = 0; i < 3; ++i) {
        RECT card = R4(SET_ROW_X, SET_ROW1_Y + i * (SET_ROW_H + SET_ROW_GAP),
                       SET_ROW_W, SET_ROW_H);
        int hit = HIT_OPT_MIN + i;
        int on = (i == 0) ? g_min_to_tray
               : (i == 1) ? g_close_to_tray : g_auto;
        int hover = (g_hover == hit);
        int press = (g_press == hit);
        COLORREF fill = press ? C_OPT_P : (hover ? C_OPT_H : C_PANEL);
        int icx = card.left + D(SET_ICON_DX) * SS;
        int icy = (card.top + card.bottom) / 2;

        round_fill(g_memdc, card, D(14) * SS, fill, C_PANEL_ED, SS);

        if (i == 0)      draw_tray_glyph(g_memdc, icx, icy, S(7), C_TXT_OFF);
        else if (i == 1) draw_box_x(g_memdc, icx, icy, S(7), C_TXT_OFF);
        else             draw_power(g_memdc, icx, icy, S(7), C_TXT_OFF, fill);

        draw_switch(set_toggle_rect(i), on, hover, press);
    }
}

/* --------------------------- HID 部分 ------------------------------------- */
static hid_device *open_device(void)
{
    struct hid_device_info *list, *it;
    hid_device *dev = NULL;

    list = hid_enumerate(DEV_VID, DEV_PID);
    for (it = list; it != NULL; it = it->next) {
        if (it->usage_page == DEV_USAGE_PAGE &&
            it->usage      == DEV_USAGE &&
            it->interface_number == DEV_INTERFACE_NUMBER) {
            dev = hid_open_path(it->path);
            break;
        }
    }
    hid_free_enumeration(list);
    return dev;
}

static void build_frame(unsigned char *frame, unsigned char cls,
                        unsigned char cmd, unsigned char size,
                        const unsigned char *args, int nargs)
{
    int i, x = 0;

    memset(frame, 0, REPORT_LEN);
    frame[2] = 0x1F;
    frame[6] = size;
    frame[7] = cls;
    frame[8] = cmd;
    for (i = 0; i < nargs && i < 80; ++i)
        frame[9 + i] = args[i];
    for (i = 3; i < 89; ++i)
        x ^= frame[i];
    frame[89] = (unsigned char)x;
}

static int send_raw(const unsigned char *frame)
{
    hid_device *dev;
    int ok;

    hid_init();
    dev = open_device();
    if (dev == NULL) {
        hid_exit();
        return 0;
    }

    ok = (hid_send_feature_report(dev, frame, REPORT_LEN) >= 0);

    hid_close(dev);
    hid_exit();
    return ok;
}

static int send_light(int state)
{
    unsigned char frame[REPORT_LEN];
    unsigned char args[9];

    args[0] = VARSTORE;
    args[1] = LED_BACKLIGHT;
    args[2] = 0xFF;
    build_frame(frame, RX_CLASS, RX_SET_BRIGHT, 3, args, 3);
    send_raw(frame);

    if (state == LIGHT_OFF) {
        args[0] = VARSTORE;
        args[1] = LED_BACKLIGHT;
        args[2] = EFF_OFF;
        args[3] = 0x00;
        args[4] = 0x00;
        args[5] = 0x00;
        build_frame(frame, RX_CLASS, RX_SET_EFFECT, 6, args, 6);
    } else if (state == LIGHT_BREATH) {
        args[0] = VARSTORE;
        args[1] = LED_BACKLIGHT;
        args[2] = EFF_BREATH;
        args[3] = 0x01;
        args[4] = 0x00;
        args[5] = 0x01;
        args[6] = COLOR_R;
        args[7] = COLOR_G;
        args[8] = COLOR_B;
        build_frame(frame, RX_CLASS, RX_SET_EFFECT, 9, args, 9);
    } else {
        args[0] = VARSTORE;
        args[1] = LED_BACKLIGHT;
        args[2] = EFF_STATIC;
        args[3] = 0x00;
        args[4] = 0x00;
        args[5] = 0x01;
        args[6] = COLOR_R;
        args[7] = COLOR_G;
        args[8] = COLOR_B;
        build_frame(frame, RX_CLASS, RX_SET_EFFECT, 9, args, 9);
    }

    return send_raw(frame);
}

static int read_light(void)
{
    unsigned char frame[REPORT_LEN];
    unsigned char args[3];
    hid_device *dev;
    int n, effect = -1;

    hid_init();
    dev = open_device();
    if (dev == NULL) {
        hid_exit();
        return LIGHT_OFF;
    }

    args[0] = VARSTORE;
    args[1] = LED_BACKLIGHT;
    build_frame(frame, RX_CLASS, RX_GET_EFFECT, 3, args, 2);
    if (hid_send_feature_report(dev, frame, REPORT_LEN) >= 0) {
        int attempt;
        for (attempt = 0; attempt < 2; ++attempt) {
            Sleep(250);
            memset(frame, 0, sizeof(frame));
            frame[0] = 0x00;
            n = hid_get_feature_report(dev, frame, REPORT_LEN);
            if (n >= 12 && frame[1] == 0x02 && frame[7] == RX_CLASS &&
                (frame[8] == RX_GET_EFFECT || frame[8] == RX_SET_EFFECT)) {
                effect = frame[11];
                break;
            }
            if (attempt == 0) {
                build_frame(frame, RX_CLASS, RX_GET_EFFECT, 3, args, 2);
                hid_send_feature_report(dev, frame, REPORT_LEN);
            }
        }
    }

    hid_close(dev);
    hid_exit();

    if (effect == EFF_BREATH) return LIGHT_BREATH;
    if (effect == EFF_STATIC) return LIGHT_ON;
    if (effect == EFF_OFF)    return LIGHT_OFF;
    return (effect >= 0) ? LIGHT_ON : LIGHT_OFF;
}

/* --------------------------- 开机自启 ------------------------------------- */
#define RUN_ARG L"--autostart"

static int auto_query(void)
{
    wchar_t buf[2 * MAX_PATH];
    DWORD   cb = sizeof(buf);
    HKEY    key;
    LONG    r;

    r = RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_QUERY_VALUE, &key);
    if (r != ERROR_SUCCESS) return 0;

    r = RegQueryValueExW(key, RUN_VAL, NULL, NULL, (LPBYTE)buf, &cb);
    RegCloseKey(key);
    return (r == ERROR_SUCCESS);
}

static int auto_set(int on)
{
    wchar_t path[2 * MAX_PATH];
    wchar_t cmd[2 * MAX_PATH + 16];
    HKEY    key;
    LONG    r;

    r = RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, NULL, 0,
                        KEY_SET_VALUE | KEY_QUERY_VALUE, NULL, &key, NULL);
    if (r != ERROR_SUCCESS) return 0;

    if (on) {
        GetModuleFileNameW(NULL, path, 2 * MAX_PATH);
        wsprintfW(cmd, L"\"%s\" " RUN_ARG, path);
        r = RegSetValueExW(key, RUN_VAL, 0, REG_SZ,
                           (const BYTE *)cmd,
                           (DWORD)((lstrlenW(cmd) + 1) * sizeof(wchar_t)));
    } else {
        r = RegDeleteValueW(key, RUN_VAL);
        if (r == ERROR_FILE_NOT_FOUND) r = ERROR_SUCCESS;
    }

    RegCloseKey(key);
    return (r == ERROR_SUCCESS);
}

/* 旧版本写的是 --silent（只发一次报文就退出），启动时顺手迁移成 --autostart */
static void auto_sync_command(void)
{
    wchar_t buf[2 * MAX_PATH];
    DWORD   cb = sizeof(buf);
    HKEY    key;
    int     need_sync = 0;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0,
                      KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return;

    if (RegQueryValueExW(key, RUN_VAL, NULL, NULL, (LPBYTE)buf, &cb) ==
            ERROR_SUCCESS && cb >= sizeof(wchar_t) && cb <= sizeof(buf)) {
        buf[cb / sizeof(wchar_t) - 1] = 0;
        need_sync = (wcsstr(buf, RUN_ARG) == NULL);
    }

    RegCloseKey(key);
    if (need_sync) auto_set(1);
}

/* --------------------------- 定时持久化 ----------------------------------- */
static void save_timer_next(void)
{
    HKEY key;
    ULONGLONG v = (g_timer_on && g_next_fire > 0)
                    ? (ULONGLONG)g_next_fire : (ULONGLONG)0;
    DWORD daily = (DWORD)(g_timer_daily ? 1 : 0);

    if (RegCreateKeyExW(HKEY_CURRENT_USER, CONF_KEY, 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS)
        return;

    RegSetValueExW(key, TIMER_NEXT, 0, REG_QWORD,
                   (const BYTE *)&v, sizeof(v));
    RegSetValueExW(key, TIMER_DAILY, 0, REG_DWORD,
                   (const BYTE *)&daily, sizeof(daily));
    RegCloseKey(key);
}

static void load_timer_next(void)
{
    HKEY key;
    ULONGLONG v = 0;
    DWORD cb = sizeof(v);
    DWORD type = 0;
    DWORD daily = 0;
    DWORD dcb = sizeof(daily);
    DWORD dtype = 0;
    time_t now = time(NULL);

    g_timer_on = 0;
    g_timer_daily = 0;
    g_edit_daily = 0;
    g_next_fire = 0;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, CONF_KEY, 0,
                      KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return;

    if (RegQueryValueExW(key, TIMER_DAILY, NULL, &dtype,
                         (LPBYTE)&daily, &dcb) == ERROR_SUCCESS &&
        dtype == REG_DWORD && daily != 0) {
        g_timer_daily = 1;
    }

    if (RegQueryValueExW(key, TIMER_NEXT, NULL, &type,
                         (LPBYTE)&v, &cb) == ERROR_SUCCESS &&
        type == REG_QWORD) {
        if (v > (ULONGLONG)now) {
            struct tm *lt;
            g_next_fire = (time_t)v;
            g_timer_on = 1;
            lt = localtime(&g_next_fire);
            if (lt != NULL) {
                g_timer_hour = lt->tm_hour;
                g_timer_min = lt->tm_min;
            }
        } else if (v != 0 && g_timer_daily) {
            /* 程序关着时错过了一次每天定时：继续按每天的时间排下一次 */
            time_t t = (time_t)v;
            struct tm *lt = localtime(&t);
            if (lt != NULL) {
                int tomorrow = 0;
                g_timer_hour = lt->tm_hour;
                g_timer_min = lt->tm_min;
                g_next_fire = compute_next_fire(g_timer_hour, g_timer_min,
                                                &tomorrow);
                g_timer_on = (g_next_fire != 0);
                if (g_timer_on) save_timer_next();
            }
        } else if (v != 0) {
            ULONGLONG zero = 0;
            RegSetValueExW(key, TIMER_NEXT, 0, REG_QWORD,
                           (const BYTE *)&zero, sizeof(zero));
        }
    }

    g_edit_daily = g_timer_daily;
    RegCloseKey(key);
}

static time_t compute_next_fire(int hour, int minute, int *is_tomorrow)
{
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    struct tm t;
    time_t next;
    int tomorrow = 0;

    if (lt == NULL) return 0;
    t = *lt;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = 0;
    t.tm_isdst = -1;

    next = mktime(&t);
    if (next == (time_t)-1) return 0;

    if (next <= now) {
        t.tm_mday += 1;
        t.tm_isdst = -1;
        next = mktime(&t);
        tomorrow = 1;
    }

    if (is_tomorrow) *is_tomorrow = tomorrow;
    return next;
}

/* --------------------------- 设置项持久化 --------------------------------- */
static void save_option(const wchar_t *name, int on)
{
    HKEY key;
    DWORD v = (DWORD)(on ? 1 : 0);

    if (RegCreateKeyExW(HKEY_CURRENT_USER, CONF_KEY, 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS)
        return;

    RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
    RegCloseKey(key);
}

static int query_option(const wchar_t *name, int fallback)
{
    HKEY key;
    DWORD v = 0, cb = sizeof(v), type = 0;
    int result = fallback;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, CONF_KEY, 0,
                      KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return fallback;

    if (RegQueryValueExW(key, name, NULL, &type,
                         (LPBYTE)&v, &cb) == ERROR_SUCCESS && type == REG_DWORD)
        result = (v != 0);

    RegCloseKey(key);
    return result;
}

/* --------------------------- 托盘 ----------------------------------------- */
static HICON app_icon(void)
{
    HICON icon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(APP_ICON_ID));
    if (icon == NULL) icon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    return icon;
}

static void tray_add(void)
{
    NOTIFYICONDATAW nid;

    if (g_tray_added || g_hwnd == NULL) return;

    memset(&nid, 0, sizeof(nid));
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = g_hwnd;
    nid.uID              = TRAY_ID;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon            = app_icon();
    lstrcpynW(nid.szTip, APP_TITLE, 128);

    if (Shell_NotifyIconW(NIM_ADD, &nid)) g_tray_added = 1;
}

static void tray_remove(void)
{
    NOTIFYICONDATAW nid;

    if (!g_tray_added || g_hwnd == NULL) return;

    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd   = g_hwnd;
    nid.uID    = TRAY_ID;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_tray_added = 0;
}

static void hide_to_tray(void)
{
    if (g_hwnd == NULL) return;
    tray_add();
    ShowWindow(g_hwnd, SW_HIDE);
}

static void show_main_window(void)
{
    if (g_hwnd == NULL) return;
    tray_remove();
    ShowWindow(g_hwnd, IsIconic(g_hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(g_hwnd);
}

static void tray_menu(void)
{
    HMENU menu;
    POINT p;
    int cmd;

    menu = CreatePopupMenu();
    if (menu == NULL) return;

    AppendMenuW(menu, MF_STRING, TRAY_OPEN, L"打开面板");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, TRAY_EXIT, L"退出");

    GetCursorPos(&p);
    SetForegroundWindow(g_hwnd);
    cmd = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                              p.x, p.y, 0, g_hwnd, NULL);
    DestroyMenu(menu);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);

    if (cmd == TRAY_OPEN) {
        show_main_window();
    } else if (cmd == TRAY_EXIT) {
        g_quitting = 1;
        tray_remove();
        DestroyWindow(g_hwnd);
    }
}

static void minimize_window(void)
{
    if (g_min_to_tray) hide_to_tray();
    else ShowWindow(g_hwnd, SW_MINIMIZE);
}

/* --------------------------- 状态文字 ------------------------------------- */
static void set_status(const wchar_t *text, int is_error)
{
    lstrcpynW(g_status, text, 160);
    g_status_err = is_error;
    if (g_hwnd != NULL) InvalidateRect(g_hwnd, NULL, FALSE);
}

static const wchar_t *light_name(int state)
{
    if (state == LIGHT_ON)     return L"常亮";
    if (state == LIGHT_BREATH) return L"呼吸灯";
    return L"关";
}

/* --------------------------- 布局 ----------------------------------------- */
static RECT capsule_rect(int i)
{
    return R(LIGHT_X + i * (LIGHT_W + LIGHT_GAP), LIGHT_Y, LIGHT_W, LIGHT_H);
}

static RECT sleep_rect(void)  { return R(ROW_X, SLEEP_Y, ROW_W, ROW_H); }
static RECT timer_rect(void)  { return R(ROW_X, TIMER_Y, ROW_W, ROW_H); }
static RECT status_rect(void)
{
    return R(20, g_timer_open ? STATUS_Y_EXP : STATUS_Y, WIN_W - 40, STATUS_H);
}
static RECT hour_up_rect(void)
{
    return R(STEP_X1, STEP_UP_Y, STEP_W, STEP_BTN_H);
}
static RECT hour_down_rect(void)
{
    return R(STEP_X1, STEP_DOWN_Y, STEP_W, STEP_BTN_H);
}
static RECT min_up_rect(void)
{
    return R(STEP_X2, STEP_UP_Y, STEP_W, STEP_BTN_H);
}
static RECT min_down_rect(void)
{
    return R(STEP_X2, STEP_DOWN_Y, STEP_W, STEP_BTN_H);
}
static RECT primary_rect(void)
{
    return R(PRIMARY_X, ACT_Y, PRIMARY_W, ACT_H);
}
static RECT secondary_rect(void)
{
    return R(SECONDARY_X, ACT_Y, SECONDARY_W, ACT_H);
}
static RECT daily_rect(void)
{
    return R(DAILY_X, ACT_Y, DAILY_W, ACT_H);
}

/* --------------------------- 命中测试 ------------------------------------- */
static int hit_test(POINT p)
{
    RECT r;
    int i;

    r = R(WIN_W - 45, 17, 30, 30);
    if (PtInRect(&r, p)) return HIT_CLOSE;

    r = R(WIN_W - 81, 17, 30, 30);
    if (PtInRect(&r, p)) return HIT_MIN;

    if (g_view == VIEW_SETTINGS) {
        r = back_rect();     if (PtInRect(&r, p)) return HIT_BACK;
        r = set_row_rect(0); if (PtInRect(&r, p)) return HIT_OPT_MIN;
        r = set_row_rect(1); if (PtInRect(&r, p)) return HIT_OPT_CLOSE;
        r = set_row_rect(2); if (PtInRect(&r, p)) return HIT_OPT_AUTO;
        return HIT_NONE;
    }

    r = gear_rect();
    if (PtInRect(&r, p)) return HIT_SETTINGS;

    if (g_timer_open) {
        r = hour_up_rect();   if (PtInRect(&r, p)) return HIT_HOUR_UP;
        r = hour_down_rect(); if (PtInRect(&r, p)) return HIT_HOUR_DOWN;
        r = min_up_rect();    if (PtInRect(&r, p)) return HIT_MIN_UP;
        r = min_down_rect();  if (PtInRect(&r, p)) return HIT_MIN_DOWN;
        r = primary_rect();   if (PtInRect(&r, p)) return HIT_TIMER_PRIMARY;
        r = daily_rect();     if (PtInRect(&r, p)) return HIT_TIMER_DAILY;
        r = secondary_rect(); if (PtInRect(&r, p)) return HIT_TIMER_SECONDARY;
    }

    for (i = 0; i < 3; ++i) {
        r = capsule_rect(i);
        if (PtInRect(&r, p)) return HIT_LIGHT0 + i;
    }

    r = sleep_rect(); if (PtInRect(&r, p)) return HIT_SLEEP;
    r = timer_rect(); if (PtInRect(&r, p)) return HIT_TIMER;

    return HIT_NONE;
}

static int hit_rect(int hit, RECT *out)
{
    switch (hit) {
    case HIT_LIGHT0: *out = capsule_rect(0); return 1;
    case HIT_LIGHT1: *out = capsule_rect(1); return 1;
    case HIT_LIGHT2: *out = capsule_rect(2); return 1;
    case HIT_SLEEP:  *out = sleep_rect();    return 1;
    case HIT_TIMER:  *out = timer_rect();    return 1;
    case HIT_CLOSE:  *out = R(WIN_W - 45, 17, 30, 30); return 1;
    case HIT_MIN:    *out = R(WIN_W - 81, 17, 30, 30); return 1;
    case HIT_SETTINGS: *out = gear_rect();   return 1;
    case HIT_BACK:     *out = back_rect();   return 1;
    case HIT_OPT_MIN:   *out = set_row_rect(0); return 1;
    case HIT_OPT_CLOSE: *out = set_row_rect(1); return 1;
    case HIT_OPT_AUTO:  *out = set_row_rect(2); return 1;
    case HIT_HOUR_UP:   *out = hour_up_rect(); return 1;
    case HIT_HOUR_DOWN: *out = hour_down_rect(); return 1;
    case HIT_MIN_UP:    *out = min_up_rect(); return 1;
    case HIT_MIN_DOWN:  *out = min_down_rect(); return 1;
    case HIT_TIMER_PRIMARY:   *out = primary_rect(); return 1;
    case HIT_TIMER_SECONDARY: *out = secondary_rect(); return 1;
    case HIT_TIMER_DAILY:     *out = daily_rect(); return 1;
    }
    return 0;
}

static void invalidate_hit(HWND hwnd, int hit)
{
    RECT r;

    if (hit_rect(hit, &r)) {
        InflateRect(&r, D(2), D(2));
        InvalidateRect(hwnd, &r, FALSE);
    }
}

/* --------------------------- 绘制：胶囊 / 设置行 -------------------------- */
static COLORREF pill_fill(int active, int hover, int press)
{
    if (active) {
        if (press) return C_ACCENT_P;
        if (hover) return C_ACCENT_H;
        return C_ACCENT;
    }
    if (press) return C_PILL_P;
    if (hover) return C_PILL_H;
    return C_PILL;
}

static void draw_setting_pill(RECT r, int icon,
                              int active, int focused,
                              int hover, int press,
                              int chevron)
{
    int h = r.bottom - r.top;
    int cx = r.left + S(22);
    int cy = (r.top + r.bottom) / 2;
    COLORREF fill = pill_fill(active, hover, press);
    COLORREF edge = (active || focused) ? C_ACCENT_ED : C_PILL_ED;
    COLORREF fg = active ? C_ACCENT_TX : C_TXT_OFF;

    round_fill(g_memdc, r, h / 2, fill, edge, SS);

    if (icon == 0) {
        draw_moon(g_memdc, cx, cy, S(8), fg, fill);
    } else if (icon == 1) {
        draw_clock(g_memdc, cx, cy, S(8), fg);
    } else {
        draw_power(g_memdc, cx, cy, S(8), fg, fill);
    }

    if (chevron != -2) {
        COLORREF ch = (active || focused) ? fg : C_SUB;
        draw_chevron(g_memdc, r.right - S(24), cy, ch, chevron ? -1 : 1);
    }
}

static void draw_light_capsules(void)
{
    int i;
    for (i = 0; i < 3; ++i) {
        RECT r = R4(LIGHT_X + i * (LIGHT_W + LIGHT_GAP), LIGHT_Y,
                    LIGHT_W, LIGHT_H);
        int h = r.bottom - r.top;
        int selected = (!g_sleeping && g_light == i);
        int hover = (!g_sleeping && g_hover == HIT_LIGHT0 + i);
        int press = (!g_sleeping && g_press == HIT_LIGHT0 + i);
        COLORREF fill, edge;

        if (g_sleeping) {
            fill = C_DISABLED;
            edge = C_DISABLED_ED;
        } else if (selected) {
            fill = press ? C_ACCENT_P : (hover ? C_ACCENT_H : C_ACCENT);
            edge = C_ACCENT_ED;
        } else {
            fill = press ? C_PILL_P : (hover ? C_PILL_H : C_PILL);
            edge = C_PILL_ED;
        }

        round_fill(g_memdc, r, h / 2, fill, edge, SS);
    }
}

static void draw_panel_step(RECT r, int dir, int hover, int press)
{
    int h = r.bottom - r.top;
    COLORREF fill = press ? C_PILL_P : (hover ? C_PILL_H : C_PILL);

    round_fill(g_memdc, r, h / 2, fill, C_PILL_ED, SS);
    draw_chevron(g_memdc, (r.left + r.right) / 2, (r.top + r.bottom) / 2,
                 C_TXT_OFF, dir);
}

static void draw_action(RECT r, int primary, int hover, int press)
{
    int h = r.bottom - r.top;
    COLORREF fill = primary
        ? (press ? C_ACCENT_P : (hover ? C_ACCENT_H : C_ACCENT))
        : (press ? C_PILL_P : (hover ? C_PILL_H : C_PILL));
    COLORREF edge = primary ? C_ACCENT_ED : C_PILL_ED;

    round_fill(g_memdc, r, h / 2, fill, edge, SS);
}

static void draw_timer_panel(void)
{
    RECT card = R4(ROW_X, PANEL_Y, ROW_W, PANEL_H);
    RECT val;

    round_fill(g_memdc, card, D(20) * SS, C_PANEL, C_PANEL_ED, SS);

    draw_panel_step(R4(STEP_X1, STEP_UP_Y, STEP_W, STEP_BTN_H), -1,
                    g_hover == HIT_HOUR_UP, g_press == HIT_HOUR_UP);
    draw_panel_step(R4(STEP_X1, STEP_DOWN_Y, STEP_W, STEP_BTN_H), 1,
                    g_hover == HIT_HOUR_DOWN, g_press == HIT_HOUR_DOWN);
    draw_panel_step(R4(STEP_X2, STEP_UP_Y, STEP_W, STEP_BTN_H), -1,
                    g_hover == HIT_MIN_UP, g_press == HIT_MIN_UP);
    draw_panel_step(R4(STEP_X2, STEP_DOWN_Y, STEP_W, STEP_BTN_H), 1,
                    g_hover == HIT_MIN_DOWN, g_press == HIT_MIN_DOWN);

    val = R4(STEP_X1, STEP_VAL_Y, STEP_W, STEP_VAL_H);
    round_fill(g_memdc, val, D(12) * SS, C_PILL, C_PILL_ED, SS);
    val = R4(STEP_X2, STEP_VAL_Y, STEP_W, STEP_VAL_H);
    round_fill(g_memdc, val, D(12) * SS, C_PILL, C_PILL_ED, SS);

    draw_action(R4(PRIMARY_X, ACT_Y, PRIMARY_W, ACT_H), 1,
                g_hover == HIT_TIMER_PRIMARY, g_press == HIT_TIMER_PRIMARY);
    draw_action(R4(DAILY_X, ACT_Y, DAILY_W, ACT_H), g_edit_daily,
                g_hover == HIT_TIMER_DAILY, g_press == HIT_TIMER_DAILY);
    draw_action(R4(SECONDARY_X, ACT_Y, SECONDARY_W, ACT_H), 0,
                g_hover == HIT_TIMER_SECONDARY, g_press == HIT_TIMER_SECONDARY);
}

static void draw_title_buttons(void)
{
    int close_cx = D(WIN_W - 30);
    int min_cx   = D(WIN_W - 66);
    int cy       = D(32);
    int rad      = D(15);
    int d        = D(5);
    COLORREF close_fill, min_fill, glyph;

    close_fill = (g_press == HIT_CLOSE) ? C_TB_PRESS :
                 (g_hover == HIT_CLOSE) ? C_TB_HOVER : C_TB_IDLE;
    min_fill   = (g_press == HIT_MIN) ? C_TB_PRESS :
                 (g_hover == HIT_MIN) ? C_TB_HOVER : C_TB_IDLE;

    fill_ellipse(g_memdc, close_cx * SS, cy * SS, rad * SS, rad * SS, close_fill);
    fill_ellipse(g_memdc, min_cx * SS, cy * SS, rad * SS, rad * SS, min_fill);

    glyph = (g_hover == HIT_CLOSE) ? C_TITLE : C_SUB;
    draw_line(g_memdc, (close_cx - d) * SS, (cy - d) * SS,
              (close_cx + d) * SS, (cy + d) * SS, glyph, D(2) * SS);
    draw_line(g_memdc, (close_cx - d) * SS, (cy + d) * SS,
              (close_cx + d) * SS, (cy - d) * SS, glyph, D(2) * SS);

    glyph = (g_hover == HIT_MIN) ? C_TITLE : C_SUB;
    draw_line(g_memdc, (min_cx - d) * SS, cy * SS,
              (min_cx + d) * SS, cy * SS, glyph, D(2) * SS);
}

static void draw_frame(void)
{
    RECT full = { 0, 0, g_buf_w, g_buf_h };
    TRIVERTEX vert[2];
    GRADIENT_RECT grad = { 0, 1 };
    int ok;

    if (g_memdc == NULL) return;

    memset(vert, 0, sizeof(vert));
    vert[0].x = 0;
    vert[0].y = 0;
    vert[0].Red   = GetRValue(C_BG_TOP) << 8;
    vert[0].Green = GetGValue(C_BG_TOP) << 8;
    vert[0].Blue  = GetBValue(C_BG_TOP) << 8;
    vert[0].Alpha = 0xFF00;

    vert[1].x = g_buf_w;
    vert[1].y = g_buf_h;
    vert[1].Red   = GetRValue(C_BG) << 8;
    vert[1].Green = GetGValue(C_BG) << 8;
    vert[1].Blue  = GetBValue(C_BG) << 8;
    vert[1].Alpha = 0xFF00;

    ok = GradientFill(g_memdc, vert, 2, &grad, 1, GRADIENT_FILL_RECT_V);
    if (!ok) fill_rect(g_memdc, full, C_BG);

    {
        RECT border = { SS, SS, g_buf_w - SS, g_buf_h - SS };
        round_outline(g_memdc, border, D(CORNER) * SS, C_EDGE, SS);
    }

    {
        int y = D(64) * SS;
        draw_line(g_memdc, D(24) * SS, y, D(WIN_W - 24) * SS, y,
                  C_PILL_ED, SS);
    }

    if (g_view == VIEW_MAIN) {
        int cx = D(34), cy = D(32);
        fill_ellipse(g_memdc, cx * SS, cy * SS, S(7), S(7), RGB(20, 46, 20));
        fill_ellipse(g_memdc, cx * SS, cy * SS, S(5), S(5), RGB(36, 92, 36));
        fill_ellipse(g_memdc, cx * SS, cy * SS, S(3), S(3), C_GREEN);
    }

    draw_title_buttons();

    if (g_view == VIEW_SETTINGS) {
        draw_back_button();
        draw_settings_page();
        return;
    }

    draw_gear_button();
    draw_light_capsules();

    draw_setting_pill(R4(ROW_X, SLEEP_Y, ROW_W, ROW_H), 0,
                      g_sleeping, 0,
                      g_hover == HIT_SLEEP, g_press == HIT_SLEEP, -2);

    draw_setting_pill(R4(ROW_X, TIMER_Y, ROW_W, ROW_H), 1,
                      g_timer_on, g_timer_open,
                      g_hover == HIT_TIMER, g_press == HIT_TIMER,
                      g_timer_open ? 1 : 0);

    if (g_timer_open) draw_timer_panel();
}

/* --------------------------- 绘制：文字 ----------------------------------- */
static void draw_text_line(HDC hdc, const wchar_t *text, RECT r, HFONT font,
                           COLORREF color, UINT flags)
{
    SelectObject(hdc, font);
    SetTextColor(hdc, color);
    DrawTextW(hdc, text, -1, &r, flags | DT_NOPREFIX);
}

static void draw_pill_text(HDC hdc, RECT r, const wchar_t *title,
                           const wchar_t *value, int active, int timer)
{
    RECT tr, vr;
    COLORREF color = active ? C_ACCENT_TX : C_TXT_OFF;

    tr.left = r.left + D(44);
    tr.top = r.top;
    tr.right = r.left + D(234);
    tr.bottom = r.bottom;
    draw_text_line(hdc, title, tr, g_font_pill, color,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (timer) {
        vr.left = r.right - D(150);
        vr.right = r.right - D(44);
    } else {
        vr.left = r.right - D(100);
        vr.right = r.right - D(24);
    }
    vr.top = r.top;
    vr.bottom = r.bottom;
    draw_text_line(hdc, value, vr, g_font_pill, color,
                   DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

static void draw_settings_text(HDC hdc)
{
    static const wchar_t *titles[3] = { L"最小化至托盘", L"关闭时最小化",
                                        L"开机自启" };
    static const wchar_t *subs[3]   = { L"总是最小化到托盘，而非任务栏。",
                                        L"最小化到系统托盘而不是关闭。",
                                        L"登录 Windows 后自动启动并切换为常亮。" };
    int i;

    for (i = 0; i < 3; ++i) {
        RECT row = set_row_rect(i);
        RECT tr, sr;

        tr.left   = row.left + D(SET_TEXT_DX);
        tr.right  = row.right - D(SET_TOG_RIGHT + SET_TOG_W + 16);
        tr.top    = row.top + D(13);
        tr.bottom = tr.top + D(24);
        draw_text_line(hdc, titles[i], tr, g_font_pill, C_TITLE,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        sr = tr;
        sr.top    = row.top + D(38);
        sr.bottom = sr.top + D(18);
        draw_text_line(hdc, subs[i], sr, g_font_sub, C_SUB,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    draw_text_line(hdc, L"托盘图标：左键恢复窗口，右键打开菜单",
                   R(SET_ROW_X, SET_HINT_Y, SET_ROW_W, 20),
                   g_font_hint, C_SUB,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    draw_text_line(hdc, g_status, R(20, SET_STATUS_Y, WIN_W - 40, STATUS_H),
                   g_font_status, g_status_err ? C_ERR : C_STATUS,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void draw_text(HDC hdc)
{
    RECT r;
    wchar_t buf[80];
    int i;

    SetBkMode(hdc, TRANSPARENT);

    if (g_view == VIEW_SETTINGS) {
        draw_text_line(hdc, L"设置", R(64, 10, 200, 34), g_font_title,
                       C_TITLE, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text_line(hdc, L"窗口与托盘行为", R(65, 38, 240, 18),
                       g_font_sub, C_SUB,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_settings_text(hdc);
        return;
    }

    draw_text_line(hdc, APP_TITLE, R(44, 10, 290, 34), g_font_title,
                   C_TITLE, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    draw_text_line(hdc, L"Razer BlackWidow Essential", R(45, 38, 290, 18),
                   g_font_sub, C_SUB, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    draw_text_line(hdc, L"设置", R(266, 17, 48, 30), g_font_label,
                   g_hover == HIT_SETTINGS ? C_TITLE : C_SUB,
                   DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    draw_text_line(hdc, L"灯光", R(32, 74, 100, 16), g_font_label,
                   g_sleeping ? C_DISABLED_TX : C_SUB,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    draw_text_line(hdc, g_sleeping ? L"睡眠模式中已锁定" : L"点选即生效",
                   R(WIN_W - 252, 74, 220, 16), g_font_hint,
                   g_sleeping ? C_DISABLED_TX : C_SUB,
                   DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    for (i = 0; i < 3; ++i) {
        RECT cr = capsule_rect(i);
        COLORREF color;

        if (g_sleeping) color = C_DISABLED_TX;
        else if (g_light == i) color = C_ACCENT_TX;
        else color = C_TXT_OFF;

        draw_text_line(hdc, light_name(i), cr, g_font_capsule, color,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    draw_pill_text(hdc, sleep_rect(), L"睡眠模式",
                   g_sleeping ? L"开" : L"关", g_sleeping, 0);

    if (g_timer_on) {
        if (g_timer_daily)
            wsprintfW(buf, L"每天 %02d:%02d", g_timer_hour, g_timer_min);
        else
            wsprintfW(buf, L"%02d:%02d", g_timer_hour, g_timer_min);
    } else {
        lstrcpynW(buf, L"关", 80);
    }
    draw_pill_text(hdc, timer_rect(), L"睡眠定时", buf, g_timer_on, 1);

    if (g_timer_open) {
        draw_text_line(hdc, L"睡眠定时", R(ROW_X + 20, PANEL_Y + 12, 240, 20),
                       g_font_pill, C_TITLE,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text_line(hdc, L"24 小时制 · 到点自动进入睡眠",
                       R(ROW_X + 20, PANEL_Y + 32, 336, 16),
                       g_font_sub, C_SUB,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        wsprintfW(buf, L"%02d", g_edit_hour);
        draw_text_line(hdc, buf, R(STEP_X1, STEP_VAL_Y, STEP_W, STEP_VAL_H),
                       g_font_big, C_TITLE,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        wsprintfW(buf, L"%02d", g_edit_min);
        draw_text_line(hdc, buf, R(STEP_X2, STEP_VAL_Y, STEP_W, STEP_VAL_H),
                       g_font_big, C_TITLE,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        draw_text_line(hdc, L":", R(210, STEP_VAL_Y, 20, STEP_VAL_H),
                       g_font_big, C_SUB,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        draw_text_line(hdc, g_timer_on ? L"保存定时" : L"开启定时",
                       R(PRIMARY_X, ACT_Y, PRIMARY_W, ACT_H),
                       g_font_action, C_ACCENT_TX,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        draw_text_line(hdc, L"每天",
                       R(DAILY_X, ACT_Y, DAILY_W, ACT_H),
                       g_font_action,
                       g_edit_daily ? C_ACCENT_TX : C_TXT_OFF,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        draw_text_line(hdc, g_timer_on ? L"关闭定时" : L"取消",
                       R(SECONDARY_X, ACT_Y, SECONDARY_W, ACT_H),
                       g_font_action, C_TXT_OFF,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    r = status_rect();
    draw_text_line(hdc, g_status, r, g_font_status,
                   g_status_err ? C_ERR : C_STATUS,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

/* --------------------------- 后台缓冲 / 窗口区域 --------------------------- */
static void destroy_backbuffer(void)
{
    if (g_memdc != NULL) {
        if (g_oldbmp != NULL) SelectObject(g_memdc, g_oldbmp);
        if (g_bmp != NULL) DeleteObject(g_bmp);
        DeleteDC(g_memdc);
    }
    g_memdc = NULL;
    g_bmp = NULL;
    g_oldbmp = NULL;
}

static int create_backbuffer(HWND hwnd)
{
    HDC screen;
    BITMAPINFO bmi;
    void *bits = NULL;
    RECT rc;

    GetClientRect(hwnd, &rc);
    g_client_w = rc.right;
    g_client_h = rc.bottom;
    if (g_client_w <= 0 || g_client_h <= 0) return 0;

    g_buf_w = g_client_w * SS;
    g_buf_h = g_client_h * SS;

    screen = GetDC(hwnd);
    if (screen == NULL) return 0;

    g_memdc = CreateCompatibleDC(screen);
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = g_buf_w;
    bmi.bmiHeader.biHeight = -g_buf_h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    g_bmp = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(hwnd, screen);

    if (g_memdc == NULL || g_bmp == NULL) {
        destroy_backbuffer();
        return 0;
    }

    g_oldbmp = SelectObject(g_memdc, g_bmp);
    return 1;
}

static void sync_client_size(HWND hwnd)
{
    RECT rc;

    GetClientRect(hwnd, &rc);
    g_client_w = rc.right;
    g_client_h = rc.bottom;
}

/* 隐藏到托盘后把工作集还给系统，下次显示时按需换回 */
static void trim_memory(void)
{
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
}

static void update_region(HWND hwnd)
{
    HRGN rgn;

    if (g_client_w <= 0 || g_client_h <= 0) return;

    rgn = CreateRoundRectRgn(0, 0, g_client_w + 1, g_client_h + 1,
                             D(CORNER) * 2, D(CORNER) * 2);
    if (rgn != NULL) {
        if (!SetWindowRgn(hwnd, rgn, TRUE)) DeleteObject(rgn);
    }
}

static void resize_to(HWND hwnd, int logical_h)
{
    RECT wr, work;
    int new_w, new_h, x, y;

    new_w = D(WIN_W);
    new_h = D(logical_h);

    GetWindowRect(hwnd, &wr);
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        work.left = 0;
        work.top = 0;
        work.right = GetSystemMetrics(SM_CXSCREEN);
        work.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    x = wr.left;
    y = wr.top;
    if (y + new_h > work.bottom - D(8))
        y = work.bottom - new_h - D(8);
    if (y < work.top + D(8))
        y = work.top + D(8);

    SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
    SetWindowPos(hwnd, NULL, x, y, new_w, new_h,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    GetClientRect(hwnd, &wr);
    g_client_w = wr.right;
    g_client_h = wr.bottom;
    destroy_backbuffer();
    update_region(hwnd);
    g_hover = HIT_NONE;
    g_press = HIT_NONE;
    SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hwnd, NULL, FALSE);
}

static void resize_for_timer(HWND hwnd, int open)
{
    g_timer_open = open;
    resize_to(hwnd, open ? EXPANDED_H : COLLAPSED_H);
}

static void resize_for_view(HWND hwnd, int view)
{
    g_view = view;
    resize_to(hwnd, view == VIEW_SETTINGS ? SETTINGS_H
                 : (g_timer_open ? EXPANDED_H : COLLAPSED_H));
}

/* --------------------------- 动作 ----------------------------------------- */
static void toggle_sleep(void)
{
    int ok;

    if (g_sleeping) {
        g_sleeping = 0;
        ok = send_light(g_light);
        set_status(ok ? L"已退出睡眠模式" : L"已退出睡眠模式（键盘未连接）",
                   ok ? 0 : 1);
    } else {
        g_sleeping = 1;
        ok = send_light(LIGHT_OFF);
        set_status(ok ? L"已进入睡眠模式，灯光已关闭"
                      : L"已进入睡眠模式（键盘未连接）",
                   ok ? 0 : 1);
    }

    if (g_hwnd != NULL) InvalidateRect(g_hwnd, NULL, FALSE);
}

static void open_timer_panel(void)
{
    if (g_timer_on) {
        g_edit_hour = g_timer_hour;
        g_edit_min = g_timer_min;
    } else {
        SYSTEMTIME st;
        GetLocalTime(&st);
        g_edit_hour = (st.wHour + 1) % 24;
        g_edit_min = 0;
    }
    g_edit_daily = g_timer_daily;

    resize_for_timer(g_hwnd, 1);
    set_status(L"选择睡眠开始时间", 0);
}

static void close_timer_panel(void)
{
    resize_for_timer(g_hwnd, 0);
    set_status(L"就绪", 0);
}

static void apply_timer(void)
{
    int tomorrow = 0;
    time_t next = compute_next_fire(g_edit_hour, g_edit_min, &tomorrow);
    wchar_t buf[80];

    if (next == 0) {
        set_status(L"时间无效", 1);
        return;
    }

    g_timer_hour = g_edit_hour;
    g_timer_min = g_edit_min;
    g_timer_daily = g_edit_daily;
    g_timer_on = 1;
    g_next_fire = next;
    save_timer_next();

    resize_for_timer(g_hwnd, 0);
    if (g_timer_daily) {
        wsprintfW(buf, L"睡眠定时已开启：每天 %02d:%02d",
                  g_timer_hour, g_timer_min);
    } else {
        wsprintfW(buf, L"睡眠定时已开启：%s %02d:%02d",
                  tomorrow ? L"明天" : L"今天",
                  g_timer_hour, g_timer_min);
    }
    set_status(buf, 0);
}

static void disable_timer(void)
{
    int was_on = g_timer_on;

    g_timer_on = 0;
    g_next_fire = 0;
    save_timer_next();
    resize_for_timer(g_hwnd, 0);
    set_status(was_on ? L"睡眠定时已关闭" : L"就绪", 0);
}

static void timer_fired(void)
{
    time_t now = time(NULL);
    int late = (g_next_fire > 0) ? (int)(now - g_next_fire) : 0;
    int daily = g_timer_daily;

    if (daily) {
        int tomorrow = 0;
        g_next_fire = compute_next_fire(g_timer_hour, g_timer_min,
                                        &tomorrow);
        g_timer_on = (g_next_fire != 0);
    } else {
        g_timer_on = 0;
        g_next_fire = 0;
    }
    save_timer_next();

    if (late > 120) {
        if (daily) {
            wchar_t buf[80];
            wsprintfW(buf, L"睡眠定时已错过，下次 %02d:%02d",
                      g_timer_hour, g_timer_min);
            set_status(buf, 1);
        } else {
            set_status(L"睡眠定时已错过", 1);
        }
    } else if (g_sleeping) {
        set_status(L"定时已触发（已在睡眠模式）", 0);
    } else {
        g_sleeping = 1;
        send_light(LIGHT_OFF);
        set_status(daily ? L"定时睡眠已开启（每天）"
                         : L"定时睡眠已开启", 0);
    }

    if (g_hwnd != NULL) InvalidateRect(g_hwnd, NULL, FALSE);
}

static void open_settings(void)
{
    if (g_view == VIEW_SETTINGS) return;
    g_timer_open = 0;
    resize_for_view(g_hwnd, VIEW_SETTINGS);
    g_hover = HIT_NONE;
    g_press = HIT_NONE;
    set_status(L"就绪", 0);
}

static void close_settings(void)
{
    resize_for_view(g_hwnd, VIEW_MAIN);
    g_hover = HIT_NONE;
    g_press = HIT_NONE;
    set_status(L"就绪", 0);
}

static void activate(int hit)
{
    if (hit == HIT_SETTINGS) {
        open_settings();
        return;
    }
    if (hit == HIT_BACK) {
        close_settings();
        return;
    }
    if (hit == HIT_OPT_MIN || hit == HIT_OPT_CLOSE) {
        int *opt = (hit == HIT_OPT_MIN) ? &g_min_to_tray : &g_close_to_tray;

        *opt = !*opt;
        if (hit == HIT_OPT_MIN) {
            save_option(OPT_MIN_TRAY, g_min_to_tray);
            set_status(g_min_to_tray ? L"最小化将进入托盘"
                                     : L"最小化恢复到任务栏", 0);
        } else {
            save_option(OPT_CLOSE_TRAY, g_close_to_tray);
            set_status(g_close_to_tray ? L"关闭窗口时最小化到托盘"
                                       : L"关闭窗口时退出程序", 0);
        }
        if (g_hwnd != NULL) InvalidateRect(g_hwnd, NULL, FALSE);
        return;
    }
    if (hit == HIT_OPT_AUTO) {
        if (auto_set(!g_auto)) {
            g_auto = auto_query();
            set_status(g_auto ? L"已加入开机自启" : L"已取消开机自启", 0);
        } else {
            set_status(L"注册表写入失败", 1);
        }
        if (g_hwnd != NULL) InvalidateRect(g_hwnd, NULL, FALSE);
        return;
    }

    if (hit >= HIT_LIGHT0 && hit <= HIT_LIGHT2) {
        int want = hit - HIT_LIGHT0;

        if (g_sleeping) {
            set_status(L"睡眠模式中，灯光已锁定", 0);
            return;
        }

        if (send_light(want)) {
            g_light = want;
            set_status(want == LIGHT_ON ? L"灯光已设为常亮"
                     : want == LIGHT_BREATH ? L"灯光已设为呼吸灯"
                     : L"灯光已关闭", 0);
        } else {
            set_status(L"没找到键盘，请检查连接", 1);
        }
    } else if (hit == HIT_SLEEP) {
        toggle_sleep();
    } else if (hit == HIT_TIMER) {
        if (g_timer_open) close_timer_panel();
        else open_timer_panel();
    } else if (hit == HIT_HOUR_UP) {
        g_edit_hour = (g_edit_hour + 1) % 24;
    } else if (hit == HIT_HOUR_DOWN) {
        g_edit_hour = (g_edit_hour + 23) % 24;
    } else if (hit == HIT_MIN_UP) {
        g_edit_min = (g_edit_min + 1) % 60;
    } else if (hit == HIT_MIN_DOWN) {
        g_edit_min = (g_edit_min + 59) % 60;
    } else if (hit == HIT_TIMER_DAILY) {
        g_edit_daily = !g_edit_daily;
    } else if (hit == HIT_TIMER_PRIMARY) {
        apply_timer();
        return;
    } else if (hit == HIT_TIMER_SECONDARY) {
        if (g_timer_on) {
            disable_timer();
        } else {
            close_timer_panel();
            set_status(L"就绪", 0);
        }
        return;
    } else if (hit == HIT_CLOSE) {
        PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
        return;
    } else if (hit == HIT_MIN) {
        minimize_window();
        return;
    }

    if (g_hwnd != NULL) InvalidateRect(g_hwnd, NULL, FALSE);
}

/* --------------------------- 单实例 --------------------------------------- */
static void activate_existing(void)
{
    HWND h = NULL;
    int i;

    for (i = 0; i < 20; ++i) {
        h = FindWindowW(APP_CLASS, NULL);
        if (h != NULL) break;
        Sleep(25);
    }
    if (h == NULL) return;

    if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
    else             ShowWindow(h, SW_SHOW);

    SetForegroundWindow(h);
    FlashWindow(h, TRUE);
}

/* --------------------------- 绘制入口 ------------------------------------- */
static void paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT pr = ps.rcPaint;
    int x = pr.left;
    int y = pr.top;
    int w, h;

    /* 后台期间缓冲已释放，真正需要绘制时再按当前客户区重建 */
    if (g_memdc == NULL && IsWindowVisible(hwnd) && !IsIconic(hwnd))
        create_backbuffer(hwnd);

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    w = pr.right - x;
    h = pr.bottom - y;
    if (w > g_client_w - x) w = g_client_w - x;
    if (h > g_client_h - y) h = g_client_h - y;

    if (g_memdc != NULL) {
        draw_frame();
        if (w > 0 && h > 0) {
            SetStretchBltMode(hdc, HALFTONE);
            SetBrushOrgEx(hdc, 0, 0, NULL);
            StretchBlt(hdc, x, y, w, h, g_memdc,
                       x * SS, y * SS, w * SS, h * SS, SRCCOPY);
        }
    } else {
        RECT rc;
        GetClientRect(hwnd, &rc);
        fill_rect(hdc, rc, C_BG);
    }

    draw_text(hdc);
    EndPaint(hwnd, &ps);
}

/* --------------------------- 窗口过程 ------------------------------------- */
static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        sync_client_size(hwnd);

        g_font_title   = make_font(15, FW_SEMIBOLD);
        g_font_sub     = make_font(8,  FW_NORMAL);
        g_font_label   = make_font(9,  FW_NORMAL);
        g_font_hint    = make_font(8,  FW_NORMAL);
        g_font_capsule = make_font(13, FW_SEMIBOLD);
        g_font_pill    = make_font(11, FW_SEMIBOLD);
        g_font_big     = make_font(24, FW_SEMIBOLD);
        g_font_action  = make_font(10, FW_SEMIBOLD);
        g_font_status  = make_font(9,  FW_NORMAL);

        g_light = read_light();
        g_auto = auto_query();
        g_min_to_tray = query_option(OPT_MIN_TRAY, 1);
        g_close_to_tray = query_option(OPT_CLOSE_TRAY, 0);
        load_timer_next();

        SetTimer(hwnd, IDT_CHECK, 1000, NULL);
        update_region(hwnd);
        SetFocus(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        paint(hwnd);
        return 0;

    case WM_TIMER:
        if (wp == IDT_CHECK && g_timer_on && g_next_fire > 0 &&
            time(NULL) >= g_next_fire) {
            timer_fired();
        } else if (wp == IDT_AUTOSTART) {
            if (send_light(LIGHT_ON)) {
                KillTimer(hwnd, IDT_AUTOSTART);
                g_autostart_tries = 0;
                g_light = LIGHT_ON;
                set_status(L"已切换为常亮", 0);
            } else if (++g_autostart_tries >= AUTOSTART_TRIES) {
                KillTimer(hwnd, IDT_AUTOSTART);
                g_autostart_tries = 0;
                set_status(L"未找到键盘，请检查连接", 1);
            }
        }
        return 0;

    case WM_MOUSEMOVE:
        if (g_dragging) {
            POINT p;
            GetCursorPos(&p);
            SetWindowPos(hwnd, NULL,
                         g_drag_window.left + (p.x - g_drag_cursor.x),
                         g_drag_window.top  + (p.y - g_drag_cursor.y),
                         0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        {
            POINT p;
            int hit;
            p.x = GET_X_LPARAM(lp);
            p.y = GET_Y_LPARAM(lp);
            hit = hit_test(p);

            if (!g_tracking) {
                TRACKMOUSEEVENT tme;
                memset(&tme, 0, sizeof(tme));
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
                g_tracking = 1;
            }

            if (hit != g_hover) {
                int prev = g_hover;

                g_hover = hit;
                invalidate_hit(hwnd, prev);
                invalidate_hit(hwnd, hit);
            }
        }
        return 0;

    case WM_MOUSELEAVE:
        g_tracking = 0;
        if (g_hover != HIT_NONE) {
            invalidate_hit(hwnd, g_hover);
            g_hover = HIT_NONE;
        }
        return 0;

    case WM_LBUTTONDOWN:
        {
            POINT p;
            int hit;
            p.x = GET_X_LPARAM(lp);
            p.y = GET_Y_LPARAM(lp);
            hit = hit_test(p);
            SetFocus(hwnd);

            if (hit != HIT_NONE) {
                g_press = hit;
                SetCapture(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (p.y < D(TITLE_H)) {
                g_dragging = 1;
                GetCursorPos(&g_drag_cursor);
                GetWindowRect(hwnd, &g_drag_window);
                SetCapture(hwnd);
                SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_SIZEALL));
            }
        }
        return 0;

    case WM_LBUTTONUP:
        if (g_dragging) {
            g_dragging = 0;
            ReleaseCapture();
            return 0;
        }
        if (g_press != HIT_NONE) {
            POINT p;
            int press = g_press;
            int hit;

            p.x = GET_X_LPARAM(lp);
            p.y = GET_Y_LPARAM(lp);
            hit = hit_test(p);

            g_press = HIT_NONE;
            ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);

            if (hit == press) activate(hit);
        }
        return 0;

    case WM_CAPTURECHANGED:
        if (g_dragging) g_dragging = 0;
        if (g_press != HIT_NONE) {
            g_press = HIT_NONE;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT p;
            int hit;
            GetCursorPos(&p);
            ScreenToClient(hwnd, &p);
            hit = hit_test(p);

            if (g_dragging) {
                SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_SIZEALL));
            } else if (hit >= HIT_LIGHT0 && hit <= HIT_LIGHT2 && g_sleeping) {
                SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_ARROW));
            } else if (hit != HIT_NONE) {
                SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_HAND));
            } else if (p.y < D(TITLE_H)) {
                SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_SIZEALL));
            } else {
                SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_ARROW));
            }
            return TRUE;
        }
        break;

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            if (g_view == VIEW_SETTINGS) {
                close_settings();
            } else if (g_timer_open) {
                close_timer_panel();
                set_status(L"就绪", 0);
            } else {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
            return 0;
        }
        break;

    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_MINIMIZE && g_min_to_tray) {
            hide_to_tray();
            return 0;
        }
        break;

    case WM_TRAY:
        if (LOWORD(lp) == WM_LBUTTONUP || LOWORD(lp) == WM_LBUTTONDBLCLK) {
            show_main_window();
        } else if (LOWORD(lp) == WM_RBUTTONUP) {
            tray_menu();
        }
        return 0;

    case WM_SHOWWINDOW:
        if (wp) {
            tray_remove();
        } else {
            /* 托盘/最小化期间只需要定时器，渲染缓冲和工作集都可以先还回去 */
            destroy_backbuffer();
            trim_memory();
        }
        return 0;

    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) {
            destroy_backbuffer();
            trim_memory();
        }
        return 0;

    case WM_CLOSE:
        if (g_close_to_tray && !g_quitting) {
            hide_to_tray();
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, IDT_CHECK);
        tray_remove();
        if (g_font_title   != NULL) DeleteObject(g_font_title);
        if (g_font_sub     != NULL) DeleteObject(g_font_sub);
        if (g_font_label   != NULL) DeleteObject(g_font_label);
        if (g_font_hint    != NULL) DeleteObject(g_font_hint);
        if (g_font_capsule != NULL) DeleteObject(g_font_capsule);
        if (g_font_pill    != NULL) DeleteObject(g_font_pill);
        if (g_font_big     != NULL) DeleteObject(g_font_big);
        if (g_font_action  != NULL) DeleteObject(g_font_action);
        if (g_font_status  != NULL) DeleteObject(g_font_status);

        destroy_backbuffer();
        g_hwnd = NULL;
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* --------------------------- 入口 ----------------------------------------- */
int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    WNDCLASSEXW wc;
    MSG msg;
    HWND hwnd;
    HANDLE mutex;
    HDC screen;
    RECT work;
    int silent;
    int autostart;
    int w, h, x, y;

    (void)prev;
    (void)show;

    silent = (cmdline != NULL && strstr(cmdline, "--silent") != NULL);
    autostart = (cmdline != NULL && strstr(cmdline, "--autostart") != NULL);
    if (autostart) silent = 0;

    SetLastError(0);
    mutex = CreateMutexW(NULL, FALSE, MUTEX_NAME);
    if (mutex == NULL) return 1;

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        /* 登录自启时已有实例在跑：安静退出，不抢前台 */
        if (!silent && !autostart) activate_existing();
        CloseHandle(mutex);
        return 0;
    }

    auto_sync_command();

    if (silent) {
        send_light(LIGHT_ON);
        CloseHandle(mutex);
        return 0;
    }

    SetProcessDPIAware();
    screen = GetDC(NULL);
    g_dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(NULL, screen);
    if (g_dpi < 96) g_dpi = 96;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = APP_CLASS;
    wc.hIcon         = LoadIconW(inst, MAKEINTRESOURCEW(APP_ICON_ID));
    if (wc.hIcon == NULL) wc.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    wc.hIconSm       = wc.hIcon;
    if (!RegisterClassExW(&wc)) {
        CloseHandle(mutex);
        return 1;
    }

    w = D(WIN_W);
    h = D(COLLAPSED_H);
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        work.left = 0;
        work.top = 0;
        work.right = GetSystemMetrics(SM_CXSCREEN);
        work.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    x = work.left + (work.right - work.left - w) / 2;
    y = work.top + (work.bottom - work.top - h) / 2;

    hwnd = CreateWindowExW(WS_EX_APPWINDOW, APP_CLASS, APP_TITLE,
                           WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                           x, y, w, h, NULL, NULL, inst, NULL);
    if (hwnd == NULL) {
        CloseHandle(mutex);
        return 1;
    }

    if (autostart) {
        /* 登录启动：不弹窗口，直接进托盘，并把背光切到常亮 */
        tray_add();
        if (!g_tray_added) {
            /* 托盘图标没加上时宁可显示窗口，也不要留下一个看不见的进程 */
            ShowWindow(hwnd, SW_SHOW);
            UpdateWindow(hwnd);
        }
        if (send_light(LIGHT_ON)) {
            g_light = LIGHT_ON;
            set_status(L"已切换为常亮", 0);
        } else {
            g_autostart_tries = 0;
            SetTimer(hwnd, IDT_AUTOSTART, 1000, NULL);
        }
        if (!IsWindowVisible(hwnd)) trim_memory();
    } else {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        SetForegroundWindow(hwnd);
    }

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CloseHandle(mutex);
    return 0;
}
