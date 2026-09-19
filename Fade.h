// Fade.h — 淡入/淡出算法 (inline 数学库)
// 对应 Python: fade_in.py + fade_out.py
#pragma once

#include <algorithm>

namespace duck {

inline float clampPeak(float peak, float maxPeak) {
    return std::clamp(peak, 0.0f, maxPeak);
}

// Peak → 目标音量 (Peak 越大 → 越接近 minTargetVol)
inline float computeFadeOutTarget(float peak, float threshold, float maxPeak,
                                   float maxVol, float minTargetVol) {
    peak = clampPeak(peak, maxPeak);
    if (peak < threshold) return maxVol;
    float norm = (peak - threshold) / (maxPeak - threshold);
    norm = std::clamp(norm, 0.0f, 1.0f);
    return maxVol - norm * (maxVol - minTargetVol);
}

// Peak → 压低速度 Alpha (Peak 越大 → alpha 越接近 max)
inline float computeFadeOutAlpha(float peak, float maxPeak,
                                  float alphaMin, float alphaMax) {
    peak = clampPeak(peak, maxPeak);
    float alpha = alphaMin + peak * (alphaMax - alphaMin);
    return std::clamp(alpha, alphaMin, alphaMax);
}

struct FadeOutResult {
    float newVol;
    float target;
    float alpha;
};

inline FadeOutResult fadeOutStep(float currentVol, float peak, float threshold,
                                  float maxPeak, float maxVol, float minTargetVol,
                                  float alphaMin, float alphaMax) {
    peak = clampPeak(peak, maxPeak);
    float target = std::min(currentVol,
                            computeFadeOutTarget(peak, threshold, maxPeak,
                                                  maxVol, minTargetVol));
    float alpha = computeFadeOutAlpha(peak, maxPeak, alphaMin, alphaMax);
    return {currentVol + alpha * (target - currentVol), target, alpha};
}

// vol_next = vol_curr + release_alpha * (max_vol - vol_curr)
inline float fadeInStep(float currentVol, float maxVol, float releaseAlpha) {
    return currentVol + releaseAlpha * (maxVol - currentVol);
}

} // namespace duck
