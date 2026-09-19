// Config.h — 运行时配置 (默认值 + 热加载)
// 对应 Python: config.py + config_loader.py
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>     // GetModuleFileNameW (用于 FindSettingsJsonPath)

#include <filesystem>
#include <string>
#include <vector>

namespace duck {

struct Config {
    // 进程识别
    std::vector<std::string> targetMusicApps = {
        "cloudmusic", "qqmusic", "spotify", "foobar2000"
    };

    // 触发参数
    float triggerThreshold = 0.02f;
    float maxPeak          = 0.10f;

    // 音量参数
    float maxVol           = 1.0f;
    float minTargetVol     = 0.20f;

    // Alpha
    float attackAlphaMin   = 0.08f;
    float attackAlphaMax   = 0.35f;
    float releaseAlpha     = 0.03f;

    // 时序
    float floorHoldSec     = 5.0f;
    float pollInterval     = 0.02f;

    // 从 settings.json 加载 (热加载; 缺失/失败时保留默认值)
    static Config load(const std::wstring& path = L"settings.json");
};

// 查找 settings.json 的实际路径:
//  1. 当前工作目录 (CWD) — 通常用户直接双击 exe 时就是这里
//  2. exe 所在目录 — 便于从其它目录启动时也能找到配置
//  3. 兜底: CWD (可能不存在, 由 ifstream 失败回退到默认值)
inline std::wstring FindSettingsJsonPath() {
    namespace fs = std::filesystem;

    // 1. CWD
    if (fs::exists(L"settings.json")) return L"settings.json";

    // 2. exe 所在目录
    wchar_t exePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0) {
        fs::path p = fs::path(exePath).parent_path() / L"settings.json";
        if (fs::exists(p)) return p.wstring();
    }

    // 3. 兜底
    return L"settings.json";
}

} // namespace duck