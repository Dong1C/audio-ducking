// UI.cpp — Win32 系统托盘 + 设置窗口 (模型对话框) — 现代深色主题
// 对应 Python: (无) — 全新模块
#include "UI.h"
#include "SettingsPanel.h"
#include "Config.h"

// ───── Windows / STL ─────
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>        // GET_X_LPARAM / GET_Y_LPARAM
#include <shellapi.h>       // Shell_NotifyIcon
#include <commctrl.h>       // InitCommonControlsEx / msctls_trackbar32 / SetWindowSubclass
#include <objbase.h>        // GDI+ 需要 COM 基类型 (PROPID 等) — 必须在 <gdiplus.h> 之前
#include <gdiplus.h>        // GDI+ — 自定义绘状态指示器 (渐变胶囊)
#include <dwmapi.h>         // DWM — 沉浸式暗色标题栏 + 圆角窗口

// Common Controls v6: 启用视觉样式 (圆角按钮 / 现代 Trackbar / 现代 Edit)
#if defined(_MSC_VER)
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")

// 旧 SDK 上 DWMWA_USE_IMMERSIVE_DARK_MODE 可能未定义
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 19
#endif

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace duck {

// ───── 静态成员定义 ─────
std::atomic<bool> UI::enabled{true};
std::atomic<bool> UI::shouldExit{false};

// ───── 控件 ID 约定 (与 SettingsPanel.h 顺序对应) ─────
namespace {

// 自定义消息
constexpr UINT WM_APP_COMMIT_PARAM = WM_APP + 1;  // 编辑框回车提交, wParam=index
constexpr UINT WM_APP_REFRESH_UI   = WM_APP + 2;  // 刷新按钮文字与状态标签

constexpr UINT ID_BTN_TOGGLE        = 1001;       // 替换原 checkbox
constexpr UINT ID_STATUS_LABEL      = 1002;       // SS_OWNERDRAW 状态指示器
constexpr UINT ID_SLIDER_FLOAT_BASE = 1100;       // +0..+8 -> 9 个滑块
constexpr UINT ID_EDIT_FLOAT_BASE   = 1010;       // +0..+8 -> 9 个编辑框
constexpr UINT ID_BTN_SAVE          = 1020;
constexpr UINT ID_BTN_APPLY         = 1023;       // 应用 (写盘, 不关闭)
constexpr UINT ID_BTN_CANCEL        = 1021;
constexpr UINT ID_STATUS_BAR        = 1022;       // 底部状态栏 (SS_LEFTNOWORDWRAP)
constexpr UINT ID_TIMER_REFRESH     = 1;          // 100ms 定时器

constexpr UINT WM_USER_TRAY = WM_USER + 1;
constexpr UINT ID_TRAY_ICON = 1;

// ───── 现代 UI 布局常量 ─────
constexpr int ROW_H         = 32;
constexpr int MARGIN        = 16;
constexpr int HEADER_H      = 44;
constexpr int STATUS_BAR_H  = 30;
constexpr int BTN_H         = 32;
constexpr int BTN_W         = 88;
constexpr int PILL_W        = 180;
constexpr int PILL_H        = 36;
constexpr int TOGGLE_BTN_W  = 130;
constexpr int LABEL_W       = 220;
constexpr int SLIDER_W      = 320;
constexpr int EDIT_W        = 110;

// 目标窗口尺寸
constexpr int WINDOW_W      = 740;
constexpr int WINDOW_H      = 580;

// 配色
constexpr COLORREF CLR_BG_DARK   = RGB(30, 30, 30);     // #1E1E1E
constexpr COLORREF CLR_TEXT_HI   = RGB(220, 220, 220);
constexpr COLORREF CLR_STATUS_OK = RGB(160, 230, 160);

constexpr LPCWSTR kMainWndClass   = L"AudioDuckingMainWnd";
constexpr LPCWSTR kSettingsClass  = L"AudioDuckingSettingsWnd";
constexpr LPCWSTR kSettingsTitle  = L"Audio Ducking — 设置";
constexpr LPCWSTR kMainWndTitle   = L"AudioDucking";

// ───── 现代资源 (字体 / GDI+ token / 暗画刷) ─────
HFONT       g_hFont       = nullptr;  // Segoe UI 9pt
HFONT       g_hFontBold   = nullptr;  // Segoe UI Semibold 9pt
HFONT       g_hFontSmall  = nullptr;  // Segoe UI 8pt
HFONT       g_hFontMono   = nullptr;  // Consolas 9pt (状态栏)
ULONG_PTR   g_gdiplusToken = 0;
HBRUSH      g_hDarkBrush  = nullptr;  // WM_CTLCOLORSTATIC 用

void InitModernResources() {
    // GDI+ (用于自定义绘状态指示器)
    if (!g_gdiplusToken) {
        Gdiplus::GdiplusStartupInput si;
        Gdiplus::GdiplusStartup(&g_gdiplusToken, &si, nullptr);
    }

    // Segoe UI — Common Controls v6 视觉样式下文字更易读
    g_hFont = CreateFontW(
        -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g_hFontBold = CreateFontW(
        -16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Segoe UI Semibold");
    g_hFontSmall = CreateFontW(
        -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g_hFontMono = CreateFontW(
        -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        FIXED_PITCH | FF_MODERN, L"Consolas");

    // 暗背景画刷 (WM_CTLCOLORSTATIC 用)
    g_hDarkBrush = CreateSolidBrush(CLR_BG_DARK);
}

void CleanupModernResources() {
    if (g_hFont)      { DeleteObject(g_hFont);      g_hFont = nullptr; }
    if (g_hFontBold)  { DeleteObject(g_hFontBold);  g_hFontBold = nullptr; }
    if (g_hFontSmall) { DeleteObject(g_hFontSmall); g_hFontSmall = nullptr; }
    if (g_hFontMono)  { DeleteObject(g_hFontMono);  g_hFontMono = nullptr; }
    if (g_hDarkBrush) { DeleteObject(g_hDarkBrush); g_hDarkBrush = nullptr; }
    if (g_gdiplusToken) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
}

void ApplyModernFontToControl(HWND h) {
    if (h && g_hFont) SendMessageW(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
}

void ApplyModernThemeToControl(HWND h) {
    if (h) SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
}

// settings.json 路径 (单点真相, 由 FindSettingsJsonPath() 在窗口创建时解析)
std::wstring g_settingsPath;                  // 主入口 (默认空 → 用 FindSettingsJsonPath())
std::wstring g_settingsTmp;                   // 临时文件名 (写入时的 .tmp)

// ───── 工具: utf8 <-> wide ─────
std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), len);
    return out;
}

std::string wideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring toLowerWide(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](wchar_t c) { return (wchar_t)towlower(c); });
    return s;
}

std::string toLowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::string trimAscii(std::string s) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

// ───── 滑块 <-> 数值 转换 ─────
// 注意: 用 (sliderMax - min) 作为映射区间, 而非 (max - min).
// 这样典型值下 (value <= sliderMax) 滑块手感良好; value > sliderMax 时
// 自动 clamp 到右端, 用户可通过 Edit 框输入更大值.
inline int sliderValueToPos(float v, float min, float sliderMax) {
    int scale = 1000;
    if (sliderMax <= min) return 0;
    float norm = (v - min) / (sliderMax - min);
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;
    return (int)std::lround(norm * scale);
}

inline float sliderPosToValue(int pos, float min, float sliderMax) {
    int scale = 1000;
    if (sliderMax <= min) return min;
    float norm = (float)pos / (float)scale;
    return min + norm * (sliderMax - min);
}

// ───── 工具: 编辑框读文本 (UTF-8) ─────
std::string getEditTextUtf8(HWND hEdit) {
    int len = GetWindowTextLengthW(hEdit);
    if (len <= 0) return {};
    std::wstring w((size_t)len + 1, L'\0');
    GetWindowTextW(hEdit, w.data(), len + 1);
    w.resize(wcslen(w.c_str()));  // 去掉 GetWindowTextW 写入后的 \0 之后
    return wideToUtf8(w);
}

void setEditTextUtf8(HWND hEdit, const std::string& utf8) {
    SetWindowTextW(hEdit, utf8ToWide(utf8).c_str());
}

// ───── 工具: 浮点字符串解析 ─────
std::optional<float> parseFloat(const std::string& s) {
    if (s.empty()) return std::nullopt;
    try {
        size_t pos = 0;
        float v = std::stof(s, &pos);
        // 允许尾部空白, 不允许多余非数字
        while (pos < s.size() && std::isspace((unsigned char)s[pos])) ++pos;
        if (pos != s.size()) return std::nullopt;
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

// ───── 工具: 用 Config 默认值 (settings.json 缺失时回退) ─────
std::string floatToString(float v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

void fillEditFromConfigOrJson(HWND hEdit, const char* key,
                              const nlohmann::json& j, float defaultVal) {
    if (j.contains(key) && j[key].is_number()) {
        setEditTextUtf8(hEdit, floatToString(j[key].get<float>()));
    } else {
        setEditTextUtf8(hEdit, floatToString(defaultVal));
    }
}

// ───── 子过程: 拦截编辑框回车, 通知父窗口提交 ─────
LRESULT CALLBACK EditSubclassProc(HWND h, UINT m, WPARAM w, LPARAM l,
                                  UINT_PTR /*id*/, DWORD_PTR ref) {
    if (m == WM_KEYDOWN && w == VK_RETURN) {
        SendMessageW(GetParent(h), WM_APP_COMMIT_PARAM, (WPARAM)ref, 0);
        return 0;
    }
    return DefSubclassProc(h, m, w, l);
}

// ───── 主窗口实例 (UI 线程句柄; 用于 WM_USER_TRAY) ─────
HWND g_mainHwnd = nullptr;

// ───── 立即刷新"启用/暂停"按钮文字 + 状态标签 ─────
void RefreshEnableVisuals(HWND hwnd) {
    bool en = UI::enabled.load();
    SetWindowTextW(GetDlgItem(hwnd, ID_BTN_TOGGLE),
                   en ? L"暂停 Ducking" : L"启用 Ducking");
    HWND hStatus = GetDlgItem(hwnd, ID_STATUS_LABEL);
    if (hStatus) InvalidateRect(hStatus, nullptr, FALSE);   // 触发重绘
}

// ───── 设置窗口创建 (前向声明: MainWndProc 中会调用) ─────
void CreateSettingsWindow();

// ───── 设置窗口 WndProc ─────
LRESULT CALLBACK SettingsWndProc(HWND, UINT, WPARAM, LPARAM);

// ───── 主窗口 WndProc ─────
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_USER_TRAY:
        switch (LOWORD(lParam)) {
        case WM_LBUTTONDBLCLK:
            CreateSettingsWindow();
            return 0;
        case WM_RBUTTONUP: {
            // 弹出菜单: 启用 / 设置... / 退出
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);  // 让菜单能正确关闭

            HMENU hMenu = CreatePopupMenu();
            if (!hMenu) return 0;
            AppendMenuW(hMenu, MF_STRING | (UI::enabled.load() ? MF_CHECKED : 0),
                        1001, UI::enabled.load() ? L"✓ 启用" : L"启用");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, 1002, L"设置...");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, 1003, L"退出");

            int cmd = TrackPopupMenu(
                hMenu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(hMenu);

            if (cmd == 1001) {
                UI::enabled.store(!UI::enabled.load());
                HWND sw = FindWindowW(kSettingsClass, kSettingsTitle);
                if (sw) PostMessageW(sw, WM_APP_REFRESH_UI, 0, 0);
            } else if (cmd == 1002) {
                CreateSettingsWindow();
            } else if (cmd == 1003) {
                UI::shouldExit.store(true);
                PostQuitMessage(0);
            }
            return 0;
        }
        }
        return 0;

    case WM_DESTROY:
        // 隐藏窗口收到 WM_DESTROY (主循环退出触发) → 退出消息循环
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ───── 设置窗口创建 ─────
void CreateSettingsWindow() {
    // 解析 settings.json 路径 (CWD 或 exe 目录)
    if (g_settingsPath.empty()) {
        g_settingsPath = FindSettingsJsonPath();
        g_settingsTmp = g_settingsPath + L".tmp";
    }

    // 总是销毁旧窗口后再创建: 这样每次托盘双击都会从磁盘重新读取 settings.json,
    // 而不会显示陈旧的 in-memory json.
    HWND existing = FindWindowW(kSettingsClass, kSettingsTitle);
    if (existing) {
        DestroyWindow(existing);  // 同步触发 WM_DESTROY, 释放注入的 json
    }

    // 加载当前 settings.json 作为初始值
    nlohmann::json j;
    {
        std::ifstream f(g_settingsPath);
        if (f.is_open()) {
            try { f >> j; } catch (...) {}
        }
    }
    Config defaults;  // 提供默认 fallback

    // ── 屏幕居中 ──
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int x = (sw - WINDOW_W) / 2, yy = (sh - WINDOW_H) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kSettingsClass, kSettingsTitle,
        WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, yy, WINDOW_W, WINDOW_H,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) return;

    // ── DWM: 沉浸式暗标题栏 + 圆角 ──
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    // 注入初始值 + 编辑控件 ID
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)new nlohmann::json(std::move(j)));

    // ── Header 行 ──
    // 左: 状态指示器 (SS_OWNERDRAW, GDI+ 自绘渐变胶囊)
    {
        int pillX = MARGIN;
        int pillY = MARGIN + (HEADER_H - PILL_H) / 2;
        HWND hStatus = CreateWindowExW(
            0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_OWNERDRAW | SS_NOTIFY,
            pillX, pillY, PILL_W, PILL_H,
            hwnd, (HMENU)(UINT_PTR)ID_STATUS_LABEL,
            GetModuleHandleW(nullptr), nullptr);
        (void)hStatus;
    }
    // 右: 启用/暂停按钮 (TOGGLE_BTN_W × BTN_H, 视觉样式 = 现代扁平)
    {
        int toggleX = WINDOW_W - MARGIN - TOGGLE_BTN_W;
        int toggleY = MARGIN + (HEADER_H - BTN_H) / 2;
        HWND hBtn = CreateWindowExW(
            0, L"BUTTON", L"启用 Ducking",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            toggleX, toggleY, TOGGLE_BTN_W, BTN_H,
            hwnd, (HMENU)(UINT_PTR)ID_BTN_TOGGLE,
            GetModuleHandleW(nullptr), nullptr);
        ApplyModernFontToControl(hBtn);
        ApplyModernThemeToControl(hBtn);
    }

    // ── 9 个浮点参数 (滑块 + 编辑框) ──
    int floatStartY = MARGIN + HEADER_H + 14;
    int sliderX = MARGIN + LABEL_W + 6;
    int editX   = sliderX + SLIDER_W + 6;

    for (size_t i = 0; i < kFloatParamCount; ++i) {
        int rowY = floatStartY + (int)i * ROW_H;
        // Label
        std::wstring lblW = utf8ToWide(kFloatParams[i].label);
        HWND hLabel = CreateWindowExW(
            0, L"STATIC", lblW.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_LEFTNOWORDWRAP,
            MARGIN, rowY + 6, LABEL_W, ROW_H - 6,
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        ApplyModernFontToControl(hLabel);
        ApplyModernThemeToControl(hLabel);

        // Slider (Trackbar) — Common Controls v6 视觉样式 + DarkMode_Explorer
        constexpr int scale = 1000;
        // 初始值: 从 j / defaults 拿
        float initVal = kFloatParams[i].min;
        if (j.contains(kFloatParams[i].key) && j[kFloatParams[i].key].is_number()) {
            initVal = j[kFloatParams[i].key].get<float>();
        } else {
            // 从 defaults 推断
            switch (i) {
                case 0: initVal = defaults.triggerThreshold; break;
                case 1: initVal = defaults.maxPeak; break;
                case 2: initVal = defaults.maxVol; break;
                case 3: initVal = defaults.minTargetVol; break;
                case 4: initVal = defaults.attackAlphaMin; break;
                case 5: initVal = defaults.attackAlphaMax; break;
                case 6: initVal = defaults.releaseAlpha; break;
                case 7: initVal = defaults.floorHoldSec; break;
                case 8: initVal = defaults.pollInterval; break;
            }
        }
        // 滑块位置: clamp 到 [min, sliderMax]; Edit 框保留原值供编辑
        float sliderVal = std::clamp(initVal, kFloatParams[i].min, kFloatParams[i].sliderMax);

        HWND hSlider = CreateWindowExW(
            0, TRACKBAR_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS,
            sliderX, rowY + 4, SLIDER_W, ROW_H - 4,
            hwnd, (HMENU)(UINT_PTR)(ID_SLIDER_FLOAT_BASE + i),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(hSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, scale));
        SendMessageW(hSlider, TBM_SETPOS, TRUE,
                     sliderValueToPos(sliderVal, kFloatParams[i].min, kFloatParams[i].sliderMax));
        ApplyModernThemeToControl(hSlider);

        // Edit (v6 视觉样式 + Segoe UI)
        HWND hEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
            editX, rowY + 2, EDIT_W, ROW_H - 4,
            hwnd, (HMENU)(UINT_PTR)(ID_EDIT_FLOAT_BASE + i),
            GetModuleHandleW(nullptr), nullptr);
        setEditTextUtf8(hEdit, floatToString(initVal));
        ApplyModernFontToControl(hEdit);
        ApplyModernThemeToControl(hEdit);

        // 拦截回车
        SetWindowSubclass(hEdit, EditSubclassProc, 0, (DWORD_PTR)i);
    }

    // ── 应用 / 保存 / 取消 按钮 (从右往左依次排列, 视觉样式扁平) ──
    int btnY     = floatStartY + ROW_H * (int)kFloatParamCount + 24;
    int cancelX  = WINDOW_W - MARGIN - BTN_W;
    int saveX    = cancelX - BTN_W - 8;
    int applyX   = saveX   - BTN_W - 8;

    auto makeButton = [&](int id, LPCWSTR text, DWORD style, int bx) -> HWND {
        HWND h = CreateWindowExW(
            0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | style,
            bx, btnY, BTN_W, BTN_H,
            hwnd, (HMENU)(UINT_PTR)id,
            GetModuleHandleW(nullptr), nullptr);
        ApplyModernFontToControl(h);
        ApplyModernThemeToControl(h);
        return h;
    };
    makeButton(ID_BTN_CANCEL, L"取消", BS_PUSHBUTTON,    cancelX);
    makeButton(ID_BTN_SAVE,   L"保存", BS_PUSHBUTTON,    saveX);
    makeButton(ID_BTN_APPLY,  L"应用", BS_DEFPUSHBUTTON, applyX);

    // ── 底部状态栏 (SS_LEFTNOWORDWRAP, Consolas 9pt, 暗主题) ──
    int statusBarY = btnY + BTN_H + 22;
    HWND hStatusBar = CreateWindowExW(
        WS_EX_STATICEDGE, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_NOPREFIX,
        MARGIN, statusBarY, WINDOW_W - 2 * MARGIN, STATUS_BAR_H - 6,
        hwnd, (HMENU)(UINT_PTR)ID_STATUS_BAR,
        GetModuleHandleW(nullptr), nullptr);
    if (g_hFontMono) SendMessageW(hStatusBar, WM_SETFONT, (WPARAM)g_hFontMono, TRUE);
    ApplyModernThemeToControl(hStatusBar);

    // 初始化按钮文字 (避免 WM_TIMER 兜底前的瞬间空白)
    RefreshEnableVisuals(hwnd);
}

// ───── 收集并校验所有输入 ─────
struct ParsedValues {
    std::array<float, kFloatParamCount> floats{};
};

std::optional<ParsedValues> CollectAndValidate(HWND hwnd) {
    ParsedValues pv;

    // 1) 收集浮点
    for (size_t i = 0; i < kFloatParamCount; ++i) {
        HWND hEdit = GetDlgItem(hwnd, (int)(ID_EDIT_FLOAT_BASE + i));
        std::string txt = getEditTextUtf8(hEdit);
        auto v = parseFloat(txt);
        if (!v) {
            std::ostringstream oss;
            oss << kFloatParams[i].key << " 不是有效数字: \"" << txt << "\"";
            MessageBoxW(hwnd, utf8ToWide(oss.str()).c_str(),
                        L"参数错误", MB_OK | MB_ICONWARNING);
            SetFocus(hEdit);
            return std::nullopt;
        }
        if (*v < kFloatParams[i].min || *v > kFloatParams[i].max) {
            std::ostringstream oss;
            oss << kFloatParams[i].key << " 超出范围 ("
                << kFloatParams[i].min << "~" << kFloatParams[i].max
                << "): " << txt;
            MessageBoxW(hwnd, utf8ToWide(oss.str()).c_str(),
                        L"参数错误", MB_OK | MB_ICONWARNING);
            SetFocus(hEdit);
            return std::nullopt;
        }
        pv.floats[i] = *v;
    }

    // 2) 跨字段校验 (按 SettingsPanel.h 表顺序读取)
    auto V = [&](const char* k) -> float {
        for (size_t i = 0; i < kFloatParamCount; ++i) {
            if (std::string_view(kFloatParams[i].key) == k) return pv.floats[i];
        }
        return 0.0f;
    };
    float trigger = V("TRIGGER_THRESHOLD");
    float maxPeak = V("MAX_PEAK");
    float maxVol  = V("MAX_VOL");
    float minVol  = V("MIN_TARGET_VOL");
    float amin    = V("ATTACK_ALPHA_MIN");
    float amax    = V("ATTACK_ALPHA_MAX");

    if (!(maxPeak > trigger)) {
        MessageBoxW(hwnd, L"MAX_PEAK 必须大于 TRIGGER_THRESHOLD",
                    L"参数错误", MB_OK | MB_ICONWARNING);
        return std::nullopt;
    }
    if (!(maxVol > minVol)) {
        MessageBoxW(hwnd, L"MAX_VOL 必须大于 MIN_TARGET_VOL",
                    L"参数错误", MB_OK | MB_ICONWARNING);
        return std::nullopt;
    }
    if (!(amax >= amin)) {
        MessageBoxW(hwnd, L"ATTACK_ALPHA_MAX 必须大于等于 ATTACK_ALPHA_MIN",
                    L"参数错误", MB_OK | MB_ICONWARNING);
        return std::nullopt;
    }

    return pv;
}

// ───── 原子写 settings.json (保留未知键 + 保留 TARGET_MUSIC_APPS) ─────
void SaveJson(const ParsedValues& pv) {
    nlohmann::json j;
    {
        std::ifstream f(g_settingsPath);
        if (f.is_open()) {
            try { f >> j; } catch (...) { j = nlohmann::json::object(); }
        }
        if (!j.is_object()) j = nlohmann::json::object();
    }
    // 覆盖浮点字段
    for (size_t i = 0; i < kFloatParamCount; ++i) {
        j[kFloatParams[i].key] = pv.floats[i];
    }

    // 序列化: 用 4 空格缩进 + UTF-8 + trailing newline
    std::string text = j.dump(4);
    text.push_back('\n');

    // 写 tmp
    {
        std::ofstream f(g_settingsTmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            throw std::runtime_error("无法打开 settings.json.tmp 写入");
        }
        f.write(text.data(), (std::streamsize)text.size());
        f.flush();
        if (!f) {
            throw std::runtime_error("写 settings.json.tmp 失败");
        }
        // 关闭在 f 出作用域时自动发生
    }

    // 原子替换
    if (!MoveFileExW(g_settingsTmp.c_str(), g_settingsPath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        // 清理 tmp
        DeleteFileW(g_settingsTmp.c_str());
        throw std::runtime_error("MoveFileExW 替换 settings.json 失败");
    }
}

// ───── 设置窗口 WndProc ─────
LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // 100ms 定时器, 刷新按钮文字 + 状态栏文本
        SetTimer(hwnd, ID_TIMER_REFRESH, 100, nullptr);
        return 0;
    }

    case WM_TIMER: {
        if (wParam == ID_TIMER_REFRESH) {
            RefreshEnableVisuals(hwnd);   // 刷新按钮文字 + 状态标签 (redraw)
            HWND hBar = GetDlgItem(hwnd, ID_STATUS_BAR);
            if (hBar) {
                SetWindowTextW(hBar, UI::getLiveStatus().c_str());
            }
        }
        return 0;
    }

    case WM_APP_COMMIT_PARAM: {
        size_t idx = (size_t)wParam;
        if (idx >= kFloatParamCount) return 0;
        HWND hEdit = GetDlgItem(hwnd, (int)(ID_EDIT_FLOAT_BASE + idx));
        HWND hSlider = GetDlgItem(hwnd, (int)(ID_SLIDER_FLOAT_BASE + idx));
        auto v = parseFloat(getEditTextUtf8(hEdit));
        if (!v) {
            std::ostringstream oss;
            oss << kFloatParams[idx].key << ": 不是有效数字 \"" << getEditTextUtf8(hEdit) << "\"";
            MessageBoxW(hwnd, utf8ToWide(oss.str()).c_str(),
                        L"参数错误", MB_OK | MB_ICONWARNING);
            SetFocus(hEdit);
            return 0;
        }
        // Edit 框内容 clamp 到 [min, max]; 滑块视觉位置基于 sliderMax
        float clamped = std::clamp(*v, kFloatParams[idx].min, kFloatParams[idx].max);
        int pos = sliderValueToPos(clamped, kFloatParams[idx].min, kFloatParams[idx].sliderMax);
        SendMessageW(hSlider, TBM_SETPOS, TRUE, pos);
        setEditTextUtf8(hEdit, floatToString(clamped));
        return 0;
    }

    case WM_APP_REFRESH_UI: {
        RefreshEnableVisuals(hwnd);
        return 0;
    }

    case WM_HSCROLL: {
        HWND hSlider = (HWND)lParam;
        int ctrlId = GetDlgCtrlID(hSlider);
        if (ctrlId >= (int)ID_SLIDER_FLOAT_BASE &&
            ctrlId < (int)(ID_SLIDER_FLOAT_BASE + kFloatParamCount)) {
            size_t idx = (size_t)(ctrlId - ID_SLIDER_FLOAT_BASE);
            int pos = (int)SendMessageW(hSlider, TBM_GETPOS, 0, 0);
            float val = sliderPosToValue(pos, kFloatParams[idx].min, kFloatParams[idx].sliderMax);
            HWND hEdit = GetDlgItem(hwnd, (int)(ID_EDIT_FLOAT_BASE + idx));
            setEditTextUtf8(hEdit, floatToString(val));
        }
        return 0;
    }

    case WM_DRAWITEM: {
        auto* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis->CtlID == ID_STATUS_LABEL) {
            // GDI+ 自绘: 圆角胶囊 + 垂直渐变 + 中心文字
            using namespace Gdiplus;
            Graphics g(dis->hDC);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

            bool active = UI::enabled.load();
            RectF rect((REAL)dis->rcItem.left, (REAL)dis->rcItem.top,
                       (REAL)(dis->rcItem.right - dis->rcItem.left),
                       (REAL)(dis->rcItem.bottom - dis->rcItem.top));

            // 垂直渐变 (激活: 亮绿 → 深绿; 暂停: 浅灰 → 深灰)
            Color c1 = active ? Color(255, 0, 200, 83)   : Color(255, 92, 92, 92);
            Color c2 = active ? Color(255, 0, 160, 64)   : Color(255, 58, 58, 58);
            LinearGradientBrush brush(rect, c1, c2, LinearGradientModeVertical);

            // 圆角胶囊路径 (左半圆 + 右半圆)
            float radius = rect.Height / 2.0f;
            GraphicsPath path;
            path.AddArc(rect.X, rect.Y, radius * 2, rect.Height, 90, 180);
            path.AddArc(rect.X + rect.Width - radius * 2, rect.Y, radius * 2, rect.Height, 270, 180);
            path.CloseFigure();
            g.FillPath(&brush, &path);

            // 细边框 (白色 80% / 灰色 70%)
            Pen borderPen(active ? Color(255, 255, 255, 255) : Color(180, 200, 200, 200), 1.0f);
            g.DrawPath(&borderPen, &path);

            // Segoe UI Semibold 11pt 文字 (激活: 亮白; 暂停: 浅灰)
            FontFamily fontFamily(L"Segoe UI");
            Font font(&fontFamily, 11.0f, FontStyleBold);
            SolidBrush textBrush(active ? Color(255, 250, 250, 250) : Color(255, 220, 220, 220));
            StringFormat format;
            format.SetAlignment(StringAlignmentCenter);
            format.SetLineAlignment(StringAlignmentCenter);
            format.SetFormatFlags(StringFormatFlagsNoWrap);
            const wchar_t* txt = active ? L"●  正在 Ducking" : L"○  已暂停";
            g.DrawString(txt, -1, &font, rect, &format, &textBrush);
            return TRUE;
        }
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        // 暗主题: 浅文字 + 暗背景
        HDC hdc = (HDC)wParam;
        int id = GetDlgCtrlID((HWND)lParam);
        if (id == ID_STATUS_BAR) {
            SetTextColor(hdc, CLR_STATUS_OK);  // 状态栏文本: 浅绿
        } else {
            SetTextColor(hdc, CLR_TEXT_HI);    // 普通 label: 浅灰
        }
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)g_hDarkBrush;
    }

    case WM_COMMAND: {
        WORD code = HIWORD(wParam);
        WORD id   = LOWORD(wParam);

        auto doSave = [&](HWND h) -> bool {
            auto pv = CollectAndValidate(h);
            if (!pv) return false;
            try {
                SaveJson(*pv);
            } catch (const std::exception& e) {
                std::string msg = std::string("保存失败: ") + e.what();
                MessageBoxW(h, utf8ToWide(msg).c_str(),
                            L"保存失败", MB_OK | MB_ICONERROR);
                return false;
            }
            return true;
        };

        if ((id == ID_BTN_SAVE || id == ID_BTN_APPLY) && code == BN_CLICKED) {
            // 应用 / 保存: 写盘后保留窗口 (Bug #2 修复)
            doSave(hwnd);
            return 0;
        }
        if (id == ID_BTN_CANCEL && code == BN_CLICKED) {
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == ID_BTN_TOGGLE && code == BN_CLICKED) {
            UI::enabled.store(!UI::enabled.load());
            RefreshEnableVisuals(hwnd);
            return 0;
        }
        return 0;
    }

    case WM_CLOSE:
        // 视同取消
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY: {
        KillTimer(hwnd, ID_TIMER_REFRESH);
        // 释放注入的 json
        auto* pj = (nlohmann::json*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        delete pj;
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ───── 注册窗口类 ─────
bool RegisterWindowClasses() {
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = MainWndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = kMainWndClass;
    wc.hIcon         = LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);
    if (!RegisterClassExW(&wc)) return false;

    WNDCLASSEXW wcs{};
    wcs.cbSize        = sizeof(wcs);
    wcs.lpfnWndProc   = SettingsWndProc;
    wcs.hInstance     = GetModuleHandleW(nullptr);
    wcs.lpszClassName = kSettingsClass;
    // 暗色背景画刷 (#1E1E1E) — 与窗口级 DWM 暗标题栏协调
    wcs.hbrBackground = CreateSolidBrush(CLR_BG_DARK);
    wcs.hIcon         = LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);
    wcs.hCursor       = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    if (!RegisterClassExW(&wcs)) return false;

    return true;
}

// ───── 添加托盘图标 ─────
bool AddTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = hwnd;
    nid.uID              = ID_TRAY_ICON;
    nid.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_USER_TRAY;
    nid.hIcon            = LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);
    std::wstring tip = L"Audio Ducking";
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
    return Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
}

void RemoveTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = hwnd;
    nid.uID    = ID_TRAY_ICON;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

} // anonymous namespace

// ───── 跨线程状态栏接口 ─────
std::mutex UI::liveStatusMutex;
std::wstring UI::liveStatusText;

void UI::setLiveStatus(const std::wstring& w) {
    std::lock_guard<std::mutex> lock(liveStatusMutex);
    liveStatusText = w;
}

std::wstring UI::getLiveStatus() {
    std::lock_guard<std::mutex> lock(liveStatusMutex);
    return liveStatusText;
}

// ───── 线程入口 ─────
int UI::runMessageLoop() {
    // 现代资源 (字体 / GDI+ / 暗画刷) — 必须在任何窗口创建前就绪
    InitModernResources();

    if (!RegisterWindowClasses()) {
        CleanupModernResources();
        return 1;
    }

    // 建一个隐藏主窗口 (用来接收托盘消息 + 干净退出)
    g_mainHwnd = CreateWindowExW(
        0, kMainWndClass, kMainWndTitle,
        0,  // hidden: no style
        0, 0, 0, 0,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g_mainHwnd) {
        CleanupModernResources();
        return 1;
    }

    if (!AddTrayIcon(g_mainHwnd)) {
        DestroyWindow(g_mainHwnd);
        CleanupModernResources();
        return 1;
    }

    // 标准消息循环
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    RemoveTrayIcon(g_mainHwnd);
    DestroyWindow(g_mainHwnd);
    CleanupModernResources();
    return 0;
}

} // namespace duck
