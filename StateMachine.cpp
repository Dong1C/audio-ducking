// StateMachine.cpp — DUCKING / RELEASING 两态切换
#include "StateMachine.h"

#include "Fade.h"

#include <cstdio>

namespace duck {

namespace {
constexpr float AT_MAX_EPS = 0.001f;
}

DuckerStateMachine::DuckerStateMachine()
    : state_(State::Ducking), waitStart_(0.0) {}

StateStepResult DuckerStateMachine::step(float currentVol, float peak, float threshold,
                                          float maxPeak, float maxVol, float minTargetVol,
                                          float attackAlphaMin, float attackAlphaMax,
                                          float releaseAlpha, float floorHoldSec, double now) {
    if (state_ == State::Ducking) {
        return stepDucking(currentVol, peak, threshold, maxPeak, maxVol, minTargetVol,
                           attackAlphaMin, attackAlphaMax, floorHoldSec, now);
    }
    return stepReleasing(currentVol, peak, threshold, maxVol, releaseAlpha);
}

StateStepResult DuckerStateMachine::stepDucking(float currentVol, float peak, float threshold,
                                                 float maxPeak, float maxVol, float minTargetVol,
                                                 float attackAlphaMin, float attackAlphaMax,
                                                 float floorHoldSec, double now) {
    // peak >= threshold → 语音激活, 持续压低
    if (peak >= threshold) {
        waitStart_ = 0.0;  // 重置等待计时
        auto r = fadeOutStep(currentVol, peak, threshold, maxPeak, maxVol, minTargetVol,
                             attackAlphaMin, attackAlphaMax);
        return {r.newVol, r.target, r.alpha, "Ducking"};
    }

    // peak < threshold → 启动/累加等待计时
    if (waitStart_ == 0.0) waitStart_ = now;
    double elapsed = now - waitStart_;

    // 等待满 → 转入 RELEASING
    if (elapsed >= floorHoldSec) {
        state_ = State::Releasing;
        waitStart_ = 0.0;
        return {currentVol, maxVol, 0.0f, "-> Fade-In"};
    }

    char buf[32];
    std::snprintf(buf, sizeof(buf), "Wait %.1fs", elapsed);
    return {currentVol, currentVol, 0.0f, buf};
}

StateStepResult DuckerStateMachine::stepReleasing(float currentVol, float peak, float threshold,
                                                   float maxVol, float releaseAlpha) {
    // peak >= threshold → 立即转回 DUCKING
    if (peak >= threshold) {
        state_ = State::Ducking;
        waitStart_ = 0.0;
        return {currentVol, currentVol, 0.0f, "Re-Duck"};
    }

    float newVol = fadeInStep(currentVol, maxVol, releaseAlpha);

    // 回到 max_vol (容差内) → 完成
    if (newVol >= maxVol - AT_MAX_EPS) {
        newVol = maxVol;
        state_ = State::Ducking;
        return {newVol, maxVol, 0.0f, "Released"};
    }

    return {newVol, maxVol, 0.0f, "Fading"};
}

}  // namespace duck
