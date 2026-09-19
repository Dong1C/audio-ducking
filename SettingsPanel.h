// SettingsPanel.h — 设置面板参数表 (UI 单点真相: key / label / 上下限)
#pragma once

#include <string_view>

namespace duck {

// 字符串数组型参数 (仅 1 个: 目标进程白名单)
inline constexpr const char* kTargetAppsKey = "TARGET_MUSIC_APPS";

// 浮点参数表 (顺序 = UI 显示顺序 = 编辑控件 ID 顺序)
struct FloatParam {
    const char* key;    // settings.json 字段名
    const char* label;  // UI 显示标签 (中英混合)
    float min;
    float max;
};

inline constexpr FloatParam kFloatParams[] = {
    {"TRIGGER_THRESHOLD", "TRIGGER_THRESHOLD  (0.0~1.0)",            0.0f,   1.0f},
    {"MAX_PEAK",          "MAX_PEAK  (0.001~1.0)",                   0.001f, 1.0f},
    {"MAX_VOL",           "MAX_VOL  (0.0~1.0)",                      0.0f,   1.0f},
    {"MIN_TARGET_VOL",    "MIN_TARGET_VOL  (0.0~1.0)",               0.0f,   1.0f},
    {"ATTACK_ALPHA_MIN",  "ATTACK_ALPHA_MIN  (0.0~1.0)",             0.0f,   1.0f},
    {"ATTACK_ALPHA_MAX",  "ATTACK_ALPHA_MAX  (0.0~1.0)",             0.0f,   1.0f},
    {"RELEASE_ALPHA",     "RELEASE_ALPHA  (0.0~1.0)",                0.0f,   1.0f},
    {"FLOOR_HOLD_SEC",    "FLOOR_HOLD_SEC  (0.0~3600.0)",            0.0f,   3600.0f},
    {"POLL_INTERVAL",     "POLL_INTERVAL  (0.001~10.0) sec",         0.001f, 10.0f},
};

inline constexpr size_t kFloatParamCount = sizeof(kFloatParams) / sizeof(kFloatParams[0]);

} // namespace duck
