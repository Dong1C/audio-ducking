// StateMachine.h — 闪避状态机 (压低 → 等待 → 恢复)
// 对应 Python: state_machine.py
#pragma once

#include <string>

namespace duck {

struct StateStepResult {
    float newVol;
    float target;
    float alpha;
    std::string status;  // Ducking / Wait X.Xs / -> Fade-In / Re-Duck / Fading / Released
};

class DuckerStateMachine {
public:
    DuckerStateMachine();

    // 单步状态转移
    StateStepResult step(float currentVol, float peak, float threshold, float maxPeak,
                         float maxVol, float minTargetVol,
                         float attackAlphaMin, float attackAlphaMax,
                         float releaseAlpha, float floorHoldSec, double now);

private:
    enum class State { Ducking, Releasing };
    State state_;
    double waitStart_;  // 0 = 未启动

    StateStepResult stepDucking(float currentVol, float peak, float threshold,
                                 float maxPeak, float maxVol, float minTargetVol,
                                 float attackAlphaMin, float attackAlphaMax,
                                 float floorHoldSec, double now);

    StateStepResult stepReleasing(float currentVol, float peak, float threshold,
                                   float maxVol, float releaseAlpha);
};

} // namespace duck
