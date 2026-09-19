// Config.h — 运行时配置 (默认值 + 热加载)
// 对应 Python: config.py + config_loader.py
#pragma once

#include <string>
#include <vector>

namespace duck {

struct Config {
    // 进程识别
    std::vector<std::string> targetMusicApps;

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

} // namespace duck
