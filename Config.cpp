// Config.cpp — JSON 热加载 (使用 nlohmann/json via FetchContent)
#include "Config.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>

namespace duck {

namespace {

std::string toLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

}  // namespace

Config Config::load(const std::wstring& path) {
    Config c;

    // 默认参数 L"settings.json" → 走多平台路径搜索 (CWD / exe 目录)
    std::wstring actualPath = path;
    if (path == L"settings.json") {
        actualPath = FindSettingsJsonPath();
    }

    std::ifstream f(actualPath);
    if (!f.is_open()) return c;  // 文件缺失 → 返回默认值

    nlohmann::json j;
    try {
        f >> j;
    } catch (...) {
        return c;  // 解析失败 → 返回默认值
    }

    if (!j.is_object()) return c;

    auto getFloat = [&](const char* key, float& out) {
        if (j.contains(key) && j[key].is_number()) out = j[key].get<float>();
    };

    getFloat("TRIGGER_THRESHOLD", c.triggerThreshold);
    getFloat("MAX_PEAK",          c.maxPeak);
    getFloat("MAX_VOL",           c.maxVol);
    getFloat("MIN_TARGET_VOL",    c.minTargetVol);
    getFloat("ATTACK_ALPHA_MIN",  c.attackAlphaMin);
    getFloat("ATTACK_ALPHA_MAX",  c.attackAlphaMax);
    getFloat("RELEASE_ALPHA",     c.releaseAlpha);
    getFloat("FLOOR_HOLD_SEC",    c.floorHoldSec);
    getFloat("POLL_INTERVAL",     c.pollInterval);

    if (j.contains("TARGET_MUSIC_APPS") && j["TARGET_MUSIC_APPS"].is_array()) {
        for (const auto& v : j["TARGET_MUSIC_APPS"]) {
            if (v.is_string()) c.targetMusicApps.push_back(toLowerCopy(v.get<std::string>()));
        }
    }

    return c;
}

}  // namespace duck