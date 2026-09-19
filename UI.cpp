// UI.cpp — Win32 系统托盘 + 设置窗口 (模型对话框)
// 对应 Python: (无) — 全新模块
#include "UI.h"
#include "SettingsPanel.h"
#include "Config.h"

// ───── Windows / STL ─────
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>       // Shell_NotifyIcon
#include <commctrl.h>       // InitCommonControlsEx

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
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

constexpr UINT ID_CHECK_ENABLED   = 1001;
constexpr UINT ID_EDIT_FLOAT_BASE = 1010;   // +0..+8 → 9 个浮点 Edit
constexpr UINT ID_EDIT_APPS       = 1019;   // TARGET_MUSIC_APPS
constexpr UINT ID_BTN_SAVE        = 1020;
constexpr UINT ID_BTN_CANCEL      = 1021;

constexpr UINT WM_USER_TRAY = WM_USER + 1;
constexpr UINT ID_TRAY_ICON = 1;

constexpr LPCWSTR kMainWndClass   = L"AudioDuckingMainWnd";
constexpr LPCWSTR kSettingsClass  = L"AudioDuckingSettingsWnd";
constexpr LPCWSTR kSettingsTitle  = L"Audio Ducking — 设置";
constexpr LPCWSTR kMainWndTitle   = L"AudioDucking";

// settings.json 路径 (与 Config::load 默认一致)
constexpr LPCWSTR kSettingsPath = L"settings.json";
constexpr LPCWSTR kSettingsTmp  = L"settings.json.tmp";

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

// ───── 主窗口实例 (UI 线程句柄; 用于 WM_USER_TRAY) ─────
HWND g_mainHwnd = nullptr;

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
    // 单例: 若已存在则前置
    HWND existing = FindWindowW(kSettingsClass, kSettingsTitle);
    if (existing) {
        SetForegroundWindow(existing);
        ShowWindow(existing, SW_RESTORE);
        return;
    }

    // 加载当前 settings.json 作为初始值
    nlohmann::json j;
    {
        std::ifstream f(kSettingsPath);
        if (f.is_open()) {
            try { f >> j; } catch (...) {}
        }
    }
    Config defaults;  // 提供默认 fallback

    // 计算尺寸 (一行 = 24 px, 9 float + 1 apps (高 60) + 1 checkbox + 2 buttons)
    const int rowH     = 24;
    const int margin   = 10;
    const int checkH   = 24;
    const int appsRows = 4;
    const int appsH    = rowH * appsRows;
    const int btnH     = 28;
    const int labelW   = 280;
    const int editW    = 200;
    const int editX    = margin + labelW + 6;
    const int rowW     = editX + editW + margin;

    int y = margin;
    int appsY = margin + checkH + 6;
    int floatStartY = appsY + appsH + 12;
    int btnY = floatStartY + rowH * (int)kFloatParamCount + 16;

    int totalH = btnY + btnH + margin;

    // 屏幕居中
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int x = (sw - rowW) / 2, yy = (sh - totalH) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kSettingsClass, kSettingsTitle,
        WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, yy, rowW, totalH,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) return;

    // 注入初始值 + 编辑控件 ID
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)new nlohmann::json(std::move(j)));

    // ── checkbox ──
    HWND hCheck = CreateWindowExW(
        0, L"BUTTON", L"启用闪避 (取消勾选 = 暂停)",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        margin, y, rowW - 2 * margin, checkH,
        hwnd, (HMENU)(UINT_PTR)ID_CHECK_ENABLED,
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(hCheck, BM_SETCHECK,
                 UI::enabled.load() ? BST_CHECKED : BST_UNCHECKED, 0);
    y += checkH + 4;

    // ── TARGET_MUSIC_APPS (多行 Edit) ──
    CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
        margin, appsY, rowW - 2 * margin, appsH,
        hwnd, (HMENU)(UINT_PTR)ID_EDIT_APPS,
        GetModuleHandleW(nullptr), nullptr);
    {
        std::string joined;
        if (auto* pj = (nlohmann::json*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            pj && pj->contains("TARGET_MUSIC_APPS") && (*pj)["TARGET_MUSIC_APPS"].is_array()) {
            for (const auto& v : (*pj)["TARGET_MUSIC_APPS"]) {
                if (v.is_string()) {
                    if (!joined.empty()) joined += "\n";
                    joined += v.get<std::string>();
                }
            }
        } else {
            for (size_t i = 0; i < defaults.targetMusicApps.size(); ++i) {
                if (i) joined += "\n";
                joined += defaults.targetMusicApps[i];
            }
        }
        HWND hApps = GetDlgItem(hwnd, ID_EDIT_APPS);
        setEditTextUtf8(hApps, joined);
    }

    // ── 9 个浮点参数 ──
    struct FloatSlot { UINT editId; const char* key; float defVal; };
    FloatSlot slots[] = {
        {ID_EDIT_FLOAT_BASE + 0, "TRIGGER_THRESHOLD", defaults.triggerThreshold},
        {ID_EDIT_FLOAT_BASE + 1, "MAX_PEAK",          defaults.maxPeak},
        {ID_EDIT_FLOAT_BASE + 2, "MAX_VOL",           defaults.maxVol},
        {ID_EDIT_FLOAT_BASE + 3, "MIN_TARGET_VOL",    defaults.minTargetVol},
        {ID_EDIT_FLOAT_BASE + 4, "ATTACK_ALPHA_MIN",  defaults.attackAlphaMin},
        {ID_EDIT_FLOAT_BASE + 5, "ATTACK_ALPHA_MAX",  defaults.attackAlphaMax},
        {ID_EDIT_FLOAT_BASE + 6, "RELEASE_ALPHA",     defaults.releaseAlpha},
        {ID_EDIT_FLOAT_BASE + 7, "FLOOR_HOLD_SEC",    defaults.floorHoldSec},
        {ID_EDIT_FLOAT_BASE + 8, "POLL_INTERVAL",     defaults.pollInterval},
    };

    for (size_t i = 0; i < kFloatParamCount; ++i) {
        int rowY = floatStartY + (int)i * rowH;
        // Label
        std::wstring lblW = utf8ToWide(kFloatParams[i].label);
        CreateWindowExW(
            0, L"STATIC", lblW.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            margin, rowY + 4, labelW, rowH - 4,
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        // Edit
        HWND hEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
            editX, rowY, editW, rowH - 4,
            hwnd, (HMENU)(UINT_PTR)slots[i].editId,
            GetModuleHandleW(nullptr), nullptr);
        fillEditFromConfigOrJson(hEdit, slots[i].key, j, slots[i].defVal);
    }

    // ── 保存 / 取消 按钮 ──
    int btnW = 90;
    CreateWindowExW(
        0, L"BUTTON", L"保存",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        rowW - margin - 2 * btnW - 8, btnY, btnW, btnH,
        hwnd, (HMENU)(UINT_PTR)ID_BTN_SAVE,
        GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(
        0, L"BUTTON", L"取消",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        rowW - margin - btnW, btnY, btnW, btnH,
        hwnd, (HMENU)(UINT_PTR)ID_BTN_CANCEL,
        GetModuleHandleW(nullptr), nullptr);
}

// ───── 收集并校验所有输入 ─────
struct ParsedValues {
    std::array<float, kFloatParamCount> floats{};
    std::vector<std::string> apps;
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

    // 3) 收集 TARGET_MUSIC_APPS (按行 split, trim, lowercase, dedupe, 非空)
    {
        std::string raw = getEditTextUtf8(GetDlgItem(hwnd, ID_EDIT_APPS));
        std::istringstream iss(raw);
        std::string line;
        while (std::getline(iss, line)) {
            line = trimAscii(line);
            if (line.empty()) continue;
            line = toLowerAscii(line);
            // 去重
            if (std::find(pv.apps.begin(), pv.apps.end(), line) == pv.apps.end()) {
                pv.apps.push_back(std::move(line));
            }
        }
        if (pv.apps.empty()) {
            MessageBoxW(hwnd, L"TARGET_MUSIC_APPS: 至少需要一个目标音乐应用",
                        L"参数错误", MB_OK | MB_ICONWARNING);
            SetFocus(GetDlgItem(hwnd, ID_EDIT_APPS));
            return std::nullopt;
        }
    }

    return pv;
}

// ───── 原子写 settings.json (保留未知键) ─────
void SaveJson(const ParsedValues& pv) {
    nlohmann::json j;
    {
        std::ifstream f(kSettingsPath);
        if (f.is_open()) {
            try { f >> j; } catch (...) { j = nlohmann::json::object(); }
        }
        if (!j.is_object()) j = nlohmann::json::object();
    }
    // 覆盖浮点字段
    for (size_t i = 0; i < kFloatParamCount; ++i) {
        j[kFloatParams[i].key] = pv.floats[i];
    }
    // 覆盖数组字段
    j[kTargetAppsKey] = pv.apps;

    // 序列化: 用 4 空格缩进 + UTF-8 + trailing newline
    std::string text = j.dump(4);
    text.push_back('\n');

    // 写 tmp
    {
        std::ofstream f(kSettingsTmp, std::ios::binary | std::ios::trunc);
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
    if (!MoveFileExW(kSettingsTmp, kSettingsPath,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        // 清理 tmp
        DeleteFileW(kSettingsTmp);
        throw std::runtime_error("MoveFileExW 替换 settings.json 失败");
    }
}

// ───── 设置窗口 WndProc ─────
LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_COMMAND: {
        WORD code = HIWORD(wParam);
        WORD id   = LOWORD(wParam);
        if (id == ID_BTN_SAVE && code == BN_CLICKED) {
            auto pv = CollectAndValidate(hwnd);
            if (!pv) return 0;
            try {
                SaveJson(*pv);
            } catch (const std::exception& e) {
                std::string msg = std::string("保存失败: ") + e.what();
                MessageBoxW(hwnd, utf8ToWide(msg).c_str(),
                            L"保存失败", MB_OK | MB_ICONERROR);
                return 0;
            }
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == ID_BTN_CANCEL && code == BN_CLICKED) {
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == ID_CHECK_ENABLED && code == BN_CLICKED) {
            HWND hCheck = (HWND)lParam;
            bool on = (SendMessageW(hCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
            UI::enabled.store(on);
            return 0;
        }
        return 0;
    }

    case WM_CLOSE:
        // 视同取消
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY: {
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
    wcs.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
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

// ───── 线程入口 ─────
int UI::runMessageLoop() {
    if (!RegisterWindowClasses()) return 1;

    // 建一个隐藏主窗口 (用来接收托盘消息 + 干净退出)
    g_mainHwnd = CreateWindowExW(
        0, kMainWndClass, kMainWndTitle,
        0,  // hidden: no style
        0, 0, 0, 0,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g_mainHwnd) return 1;

    if (!AddTrayIcon(g_mainHwnd)) {
        DestroyWindow(g_mainHwnd);
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
    return 0;
}

} // namespace duck
